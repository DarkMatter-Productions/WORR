#include "install_root.h"

#include "archive_path.h"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <aclapi.h>
#include <windows.h>
#include <winternl.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace worr::updater {

namespace {

constexpr size_t kCopyBufferSize = size_t{64} * 1024;
std::atomic<uint64_t> temporary_sequence{0};

struct CopiedFileMeasurement {
  Sha256 sha256;
  uint64_t size = 0;
};

void MeasureCopiedBytes(CopiedFileMeasurement *measurement,
                        const unsigned char *bytes, size_t size) {
  if (!measurement)
    return;
  if (size > UINT64_MAX - measurement->size)
    throw std::runtime_error("Updater source file is too large");
  measurement->sha256.Update(bytes, size);
  measurement->size += static_cast<uint64_t>(size);
}

void ValidateCopiedFile(CopiedFileMeasurement *measurement,
                        const InstallFileExpectation *expectation,
                        std::string_view relative_path) {
  if (!expectation)
    return;
  if (!measurement || measurement->size != expectation->size)
    throw std::runtime_error("Staged file size changed while copying: " +
                             std::string(relative_path));
  if (measurement->sha256.Final() != expectation->sha256)
    throw std::runtime_error("Staged file hash changed while copying: " +
                             std::string(relative_path));
}

uint8_t ParseHexDigit(char value) {
  if (value >= '0' && value <= '9')
    return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<uint8_t>(value - 'A' + 10);
  throw std::runtime_error("Invalid SHA-256 hexadecimal digit");
}

std::vector<std::string> SplitUpdaterPath(std::string_view relative_path) {
  if (!IsSafeUpdaterRelativePath(relative_path))
    throw std::runtime_error("Unsafe updater-owned path: " +
                             std::string(relative_path));

  const std::string canonical = CanonicalUpdaterRelativePath(relative_path);
  std::vector<std::string> components;
  size_t start = 0;
  for (;;) {
    const size_t separator = canonical.find('/', start);
    if (separator == std::string::npos) {
      components.push_back(canonical.substr(start));
      return components;
    }
    components.push_back(canonical.substr(start, separator - start));
    start = separator + 1;
  }
}

std::string TemporaryLeafName() {
#if defined(_WIN32)
  const uint64_t process = GetCurrentProcessId();
#else
  const uint64_t process = static_cast<uint64_t>(getpid());
#endif
  const uint64_t sequence =
      temporary_sequence.fetch_add(1, std::memory_order_relaxed);
  return ".worr-update-" + std::to_string(process) + "-" +
         std::to_string(sequence) + ".tmp";
}

bool IsPrivateUpdaterStatePath(std::string_view relative_path) {
  constexpr std::string_view state_prefix = ".worr-update/";
  const std::string key = UpdaterPathCollisionKey(relative_path);
  return key.size() > state_prefix.size() &&
         key.compare(0, state_prefix.size(), state_prefix) == 0;
}

#if defined(_WIN32)

class UniqueHandle {
public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
  ~UniqueHandle() { Reset(); }

  UniqueHandle(UniqueHandle &&other) noexcept : handle_(other.Release()) {}
  UniqueHandle &operator=(UniqueHandle &&other) noexcept {
    if (this != &other)
      Reset(other.Release());
    return *this;
  }

  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;

  explicit operator bool() const {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }
  HANDLE Get() const { return handle_; }
  HANDLE Release() {
    const HANDLE result = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return result;
  }
  void Reset(HANDLE handle = INVALID_HANDLE_VALUE) {
    if (*this)
      CloseHandle(handle_);
    handle_ = handle;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

using NtCreateFileFunction = decltype(&NtCreateFile);
using RtlNtStatusToDosErrorFunction = decltype(&RtlNtStatusToDosError);
using NtSetInformationFileFunction = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK,
                                                       PVOID, ULONG, ULONG);

struct NativeFunctions {
  NtCreateFileFunction create_file = nullptr;
  NtSetInformationFileFunction set_information_file = nullptr;
  RtlNtStatusToDosErrorFunction status_to_error = nullptr;
};

const NativeFunctions &GetNativeFunctions() {
  static const NativeFunctions functions = [] {
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    if (!module)
      throw std::system_error(
          static_cast<int>(GetLastError()), std::system_category(),
          "Could not load Windows native filesystem support");
    NativeFunctions result;
    result.create_file = reinterpret_cast<NtCreateFileFunction>(
        GetProcAddress(module, "NtCreateFile"));
    result.set_information_file =
        reinterpret_cast<NtSetInformationFileFunction>(
            GetProcAddress(module, "NtSetInformationFile"));
    result.status_to_error = reinterpret_cast<RtlNtStatusToDosErrorFunction>(
        GetProcAddress(module, "RtlNtStatusToDosError"));
    if (!result.create_file || !result.set_information_file ||
        !result.status_to_error)
      throw std::runtime_error(
          "Windows native filesystem support is unavailable");
    return result;
  }();
  return functions;
}

DWORD StatusToWin32(NTSTATUS status) {
  return GetNativeFunctions().status_to_error(status);
}

[[noreturn]] void ThrowWindowsError(DWORD error, const std::string &operation,
                                    std::string_view relative_path = {}) {
  std::string message = operation;
  if (!relative_path.empty())
    message += ": " + std::string(relative_path);
  throw std::filesystem::filesystem_error(
      message,
      std::error_code(static_cast<int>(error), std::system_category()));
}

bool IsMissingError(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void ValidateDirectoryHandle(HANDLE handle, std::string_view relative_path) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information))
    ThrowWindowsError(GetLastError(), "Could not inspect updater directory",
                      relative_path);
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    throw std::runtime_error("Updater path contains a reparse point: " +
                             std::string(relative_path));
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    throw std::runtime_error(
        "Updater path contains a non-directory component: " +
        std::string(relative_path));
}

void ProtectPrivateObjectAcl(HANDLE handle, std::string_view relative_path,
                             bool directory, bool apply_acl = true) {
  HANDLE raw_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
    ThrowWindowsError(GetLastError(),
                      "Could not inspect updater process identity",
                      relative_path);
  UniqueHandle token(raw_token);
  DWORD token_size = 0;
  (void)GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &token_size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || token_size == 0)
    ThrowWindowsError(GetLastError(), "Could not size updater process identity",
                      relative_path);
  std::vector<unsigned char> token_storage(token_size);
  if (!GetTokenInformation(token.Get(), TokenUser, token_storage.data(),
                           token_size, &token_size))
    ThrowWindowsError(GetLastError(), "Could not read updater process identity",
                      relative_path);
  const auto *token_user =
      reinterpret_cast<const TOKEN_USER *>(token_storage.data());
  TOKEN_ELEVATION token_elevation{};
  DWORD elevation_size = sizeof(token_elevation);
  if (!GetTokenInformation(token.Get(), TokenElevation, &token_elevation,
                           elevation_size, &elevation_size))
    ThrowWindowsError(GetLastError(),
                      "Could not inspect updater process elevation",
                      relative_path);

  std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_sid_storage{};
  std::array<unsigned char, SECURITY_MAX_SID_SIZE> admin_sid_storage{};
  std::array<unsigned char, SECURITY_MAX_SID_SIZE> authenticated_sid_storage{};
  DWORD system_sid_size = static_cast<DWORD>(system_sid_storage.size());
  DWORD admin_sid_size = static_cast<DWORD>(admin_sid_storage.size());
  DWORD authenticated_sid_size =
      static_cast<DWORD>(authenticated_sid_storage.size());
  if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid_storage.data(),
                          &system_sid_size) ||
      !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
                          admin_sid_storage.data(), &admin_sid_size) ||
      !CreateWellKnownSid(WinAuthenticatedUserSid, nullptr,
                          authenticated_sid_storage.data(),
                          &authenticated_sid_size))
    ThrowWindowsError(GetLastError(),
                      "Could not construct trusted updater ACL identities",
                      relative_path);

  const bool process_is_system =
      EqualSid(token_user->User.Sid, system_sid_storage.data()) != FALSE;
  const bool allow_readonly_authenticated_users =
      token_elevation.TokenIsElevated && directory &&
      UpdaterPathCollisionKey(relative_path) == ".worr-update";
  PSID protected_owner = token_user->User.Sid;
  if (token_elevation.TokenIsElevated && !process_is_system)
    protected_owner = admin_sid_storage.data();

  struct ExpectedAce {
    PSID sid;
    DWORD mask;
    BYTE flags;
  };
  std::vector<ExpectedAce> expected_aces;
  const auto add_expected_ace = [&expected_aces](PSID sid, DWORD mask,
                                                 BYTE flags) {
    if (std::none_of(expected_aces.begin(), expected_aces.end(),
                     [sid](const ExpectedAce &existing) {
                       return EqualSid(sid, existing.sid);
                     }))
      expected_aces.push_back({sid, mask, flags});
  };
  // A split-token administrator has the same account SID in elevated and
  // unelevated processes. Granting that SID access would let an unelevated
  // process rewrite an elevated transaction's journal or rollback files.
  // Elevated state children are therefore restricted to Administrators and
  // SYSTEM. The top directory alone grants Authenticated Users a non-inherited
  // list/traverse ACE, so both split-token and over-the-shoulder UAC callers can
  // observe that no recovery journal exists without gaining access to its
  // protected children.
  constexpr DWORD readonly_directory_mask =
      FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
      READ_CONTROL | SYNCHRONIZE;
  constexpr BYTE inherited_directory_flags =
      CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE;
  if (!token_elevation.TokenIsElevated)
    add_expected_ace(token_user->User.Sid, FILE_ALL_ACCESS,
                     directory ? inherited_directory_flags : 0);
  if (allow_readonly_authenticated_users)
    add_expected_ace(authenticated_sid_storage.data(), readonly_directory_mask,
                     0);
  add_expected_ace(system_sid_storage.data(), FILE_ALL_ACCESS,
                   directory ? inherited_directory_flags : 0);
  add_expected_ace(admin_sid_storage.data(), FILE_ALL_ACCESS,
                   directory ? inherited_directory_flags : 0);
  std::vector<EXPLICIT_ACCESSW> entries(expected_aces.size());
  for (std::size_t index = 0; index < entries.size(); ++index) {
    entries[index].grfAccessPermissions = expected_aces[index].mask;
    entries[index].grfAccessMode = SET_ACCESS;
    entries[index].grfInheritance = expected_aces[index].flags;
    entries[index].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[index].Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    entries[index].Trustee.ptstrName =
        reinterpret_cast<LPWSTR>(expected_aces[index].sid);
  }
  PACL protected_acl = nullptr;
  DWORD result = SetEntriesInAclW(static_cast<ULONG>(entries.size()),
                                  entries.data(), nullptr, &protected_acl);
  if (result != ERROR_SUCCESS)
    ThrowWindowsError(result, "Could not construct updater private ACL",
                      relative_path);
  struct LocalCleanup {
    HLOCAL value;
    ~LocalCleanup() { LocalFree(value); }
  } acl_cleanup{protected_acl};
  if (apply_acl) {
    result = SetSecurityInfo(
        handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            PROTECTED_DACL_SECURITY_INFORMATION,
        protected_owner, nullptr, protected_acl, nullptr);
    if (result != ERROR_SUCCESS)
      ThrowWindowsError(result, "Could not protect updater private directory",
                        relative_path);
  }

  PSID verified_owner = nullptr;
  PACL verified_acl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  result = GetSecurityInfo(
      handle, SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &verified_owner,
      nullptr, &verified_acl, nullptr, &descriptor);
  if (result != ERROR_SUCCESS)
    ThrowWindowsError(result, "Could not verify updater private ACL",
                      relative_path);
  LocalCleanup descriptor_cleanup{descriptor};
  if (!verified_owner || !EqualSid(verified_owner, protected_owner) ||
      !verified_acl || verified_acl->AceCount != expected_aces.size())
    throw std::runtime_error(
        "Updater private directory ACL verification failed: " +
        std::string(relative_path));
  SECURITY_DESCRIPTOR_CONTROL descriptor_control = 0;
  DWORD descriptor_revision = 0;
  if (!GetSecurityDescriptorControl(descriptor, &descriptor_control,
                                    &descriptor_revision) ||
      (descriptor_control & SE_DACL_PROTECTED) == 0)
    throw std::runtime_error(
        "Updater private directory DACL is not protected: " +
        std::string(relative_path));
  std::vector<bool> matched_aces(expected_aces.size(), false);
  for (DWORD index = 0; index < verified_acl->AceCount; ++index) {
    void *raw_ace = nullptr;
    if (!GetAce(verified_acl, index, &raw_ace))
      ThrowWindowsError(GetLastError(), "Could not verify updater private ACE",
                        relative_path);
    const auto *header = static_cast<const ACE_HEADER *>(raw_ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
      throw std::runtime_error(
          "Updater private directory ACL contains an unexpected ACE: " +
          std::string(relative_path));
    const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw_ace);
    const PSID sid = const_cast<DWORD *>(&ace->SidStart);
    const auto expected = std::find_if(
        expected_aces.begin(), expected_aces.end(),
        [sid, ace, header](const ExpectedAce &candidate) {
          return EqualSid(sid, candidate.sid) && ace->Mask == candidate.mask &&
                 header->AceFlags == candidate.flags;
        });
    if (expected == expected_aces.end())
      throw std::runtime_error(
          "Updater private directory ACL contains an unexpected permission: " +
          std::string(relative_path));
    const size_t expected_index =
        static_cast<size_t>(expected - expected_aces.begin());
    if (matched_aces[expected_index])
      throw std::runtime_error(
          "Updater private directory ACL contains a duplicate identity: " +
          std::string(relative_path));
    matched_aces[expected_index] = true;
  }
  if (std::find(matched_aces.begin(), matched_aces.end(), false) !=
      matched_aces.end())
    throw std::runtime_error(
        "Updater private directory ACL is missing an expected permission: " +
        std::string(relative_path));
}

void ValidatePrivateDirectoryAcl(HANDLE handle,
                                 std::string_view relative_path) {
  ProtectPrivateObjectAcl(handle, relative_path, true);
}

void ValidatePrivateFileAcl(HANDLE handle, std::string_view relative_path) {
  ProtectPrivateObjectAcl(handle, relative_path, false);
}

void RequirePrivateFileAcl(HANDLE handle, std::string_view relative_path) {
  ProtectPrivateObjectAcl(handle, relative_path, false, false);
}

void ValidateRegularFileHandle(HANDLE handle, std::string_view relative_path) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information))
    ThrowWindowsError(GetLastError(), "Could not inspect updater file",
                      relative_path);
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    throw std::runtime_error("Updater file is a reparse point: " +
                             std::string(relative_path));
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0)
    throw std::runtime_error("Updater path is not a regular file: " +
                             std::string(relative_path));
  if (information.nNumberOfLinks > 1)
    throw std::runtime_error("Updater file has hard-link aliases: " +
                             std::string(relative_path));
}

UniqueHandle NativeOpen(HANDLE parent, std::string_view name,
                        ACCESS_MASK desired_access, ULONG disposition,
                        ULONG options, bool missing_ok,
                        ULONG share_access = FILE_SHARE_READ |
                                             FILE_SHARE_WRITE |
                                             FILE_SHARE_DELETE) {
  const std::filesystem::path native_name =
      std::filesystem::u8path(std::string(name));
  std::wstring name_storage = native_name.native();
  if (name_storage.size() > (USHRT_MAX / sizeof(wchar_t)))
    throw std::runtime_error("Updater path component is too long");

  UNICODE_STRING unicode_name{};
  unicode_name.Buffer = name_storage.data();
  unicode_name.Length =
      static_cast<USHORT>(name_storage.size() * sizeof(wchar_t));
  unicode_name.MaximumLength = unicode_name.Length;

  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, &unicode_name,
                             OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent,
                             nullptr);
  IO_STATUS_BLOCK io_status{};
  HANDLE handle = INVALID_HANDLE_VALUE;
  const NTSTATUS status = GetNativeFunctions().create_file(
      &handle, desired_access, &attributes, &io_status, nullptr,
      FILE_ATTRIBUTE_NORMAL, share_access, disposition,
      options | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr,
      0);
  if (status < 0) {
    const DWORD error = StatusToWin32(status);
    if (missing_ok && IsMissingError(error))
      return {};
    ThrowWindowsError(error, "Could not open updater-owned path", name);
  }
  return UniqueHandle(handle);
}

UniqueHandle DuplicateDirectoryHandle(HANDLE source) {
  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                       &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
    ThrowWindowsError(GetLastError(),
                      "Could not duplicate updater root handle");
  return UniqueHandle(duplicate);
}

UniqueHandle OpenDirectoryChain(HANDLE root,
                                const std::vector<std::string> &components,
                                size_t component_count, bool create,
                                bool missing_ok, bool require_private = false) {
  UniqueHandle current = DuplicateDirectoryHandle(root);
  std::string traversed;
  for (size_t index = 0; index < component_count; ++index) {
    if (!traversed.empty())
      traversed += '/';
    traversed += components[index];

    ACCESS_MASK access = FILE_LIST_DIRECTORY | FILE_TRAVERSE |
                         FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    if (require_private)
      access |= READ_CONTROL | WRITE_DAC | WRITE_OWNER;
    if (create)
      access |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD;
    UniqueHandle next = NativeOpen(current.Get(), components[index], access,
                                   create ? FILE_OPEN_IF : FILE_OPEN,
                                   FILE_DIRECTORY_FILE, missing_ok && !create);
    if (!next)
      return {};
    ValidateDirectoryHandle(next.Get(), traversed);
    if (require_private)
      ValidatePrivateDirectoryAcl(next.Get(), traversed);
    current = std::move(next);
  }
  return current;
}

UniqueHandle OpenRegularFile(HANDLE parent, std::string_view leaf,
                             ACCESS_MASK access, bool missing_ok,
                             std::string_view display_path,
                             ULONG share_access = FILE_SHARE_READ |
                                                  FILE_SHARE_WRITE |
                                                  FILE_SHARE_DELETE) {
  UniqueHandle file = NativeOpen(parent, leaf, access | SYNCHRONIZE, FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE, missing_ok,
                                 share_access);
  if (file)
    ValidateRegularFileHandle(file.Get(), display_path);
  return file;
}

UniqueHandle CreateTemporaryFile(HANDLE parent, std::string *leaf,
                                 bool private_state = false) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    *leaf = TemporaryLeafName();
    try {
      ACCESS_MASK access = GENERIC_WRITE | DELETE | SYNCHRONIZE;
      if (private_state)
        access |= READ_CONTROL | WRITE_DAC | WRITE_OWNER;
      return NativeOpen(parent, *leaf, access,
                        FILE_CREATE, FILE_NON_DIRECTORY_FILE, false);
    } catch (const std::system_error &error) {
      if (error.code().value() != ERROR_FILE_EXISTS &&
          error.code().value() != ERROR_ALREADY_EXISTS)
        throw;
    }
  }
  throw std::runtime_error(
      "Could not allocate a unique updater temporary file");
}

void DeleteOpenFile(HANDLE file) noexcept {
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                             sizeof(disposition));
}

void WriteAll(const void *data, size_t size, HANDLE destination) {
  const auto *cursor = static_cast<const unsigned char *>(data);
  while (size != 0) {
    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, MAXDWORD));
    DWORD written = 0;
    if (!WriteFile(destination, cursor, chunk, &written, nullptr))
      ThrowWindowsError(GetLastError(),
                        "Could not write updater temporary file");
    if (written == 0)
      throw std::runtime_error("Updater temporary file write made no progress");
    cursor += written;
    size -= written;
  }
}

// Both operands necessarily have the same native handle type; their direction
// is explicit at every call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void CopyFileContents(HANDLE source, HANDLE destination,
                      CopiedFileMeasurement *measurement) {
  std::array<unsigned char, kCopyBufferSize> buffer{};
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr))
      ThrowWindowsError(GetLastError(), "Could not read updater source file");
    if (read == 0)
      return;
    MeasureCopiedBytes(measurement, buffer.data(), read);
    WriteAll(buffer.data(), read, destination);
  }
}

CopiedFileMeasurement MeasureFileContents(HANDLE source) {
  CopiedFileMeasurement measurement;
  std::array<unsigned char, kCopyBufferSize> buffer{};
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr))
      ThrowWindowsError(GetLastError(), "Could not read updater source file");
    if (read == 0)
      return measurement;
    MeasureCopiedBytes(&measurement, buffer.data(), read);
  }
}

std::string ReadBoundedFile(HANDLE source, size_t maximum_size,
                            std::string_view relative_path) {
  std::string contents;
  std::array<char, kCopyBufferSize> buffer{};
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr))
      ThrowWindowsError(GetLastError(), "Could not read updater file",
                        relative_path);
    if (read == 0)
      return contents;
    if (contents.size() > maximum_size ||
        static_cast<size_t>(read) > maximum_size - contents.size())
      throw std::runtime_error("Updater file exceeds its maximum size: " +
                               std::string(relative_path));
    contents.append(buffer.data(), read);
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void RenameTemporaryFile(HANDLE file, HANDLE parent,
                         std::string_view destination_leaf) {
  const std::filesystem::path native_name =
      std::filesystem::u8path(std::string(destination_leaf));
  const std::wstring &name = native_name.native();
  const size_t name_bytes = name.size() * sizeof(wchar_t);
  if (name_bytes > MAXDWORD)
    throw std::runtime_error("Updater destination name is too long");

  const size_t structure_size =
      offsetof(FILE_RENAME_INFO, FileName) + name_bytes;
  std::vector<std::max_align_t> storage(
      (structure_size + sizeof(std::max_align_t) - 1) /
      sizeof(std::max_align_t));
  auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
  rename->ReplaceIfExists = TRUE;
  rename->RootDirectory = parent;
  rename->FileNameLength = static_cast<DWORD>(name_bytes);
  std::memcpy(rename->FileName, name.data(), name_bytes);
  constexpr ULONG file_rename_information = 10;
  for (int attempt = 0; attempt < 10; ++attempt) {
    IO_STATUS_BLOCK io_status{};
    const NTSTATUS status = GetNativeFunctions().set_information_file(
        file, &io_status, rename, static_cast<ULONG>(structure_size),
        file_rename_information);
    if (status >= 0)
      return;
    const DWORD error = StatusToWin32(status);
    if (attempt + 1 < 10 &&
        (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
         error == ERROR_LOCK_VIOLATION)) {
      Sleep(1);
      continue;
    }
    ThrowWindowsError(error, "Could not atomically replace updater file",
                      destination_leaf);
  }
}

#else

class UniqueFd {
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd() { Reset(); }

  UniqueFd(UniqueFd &&other) noexcept : fd_(other.Release()) {}
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other)
      Reset(other.Release());
    return *this;
  }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  explicit operator bool() const { return fd_ >= 0; }
  int Get() const { return fd_; }
  int Release() {
    const int result = fd_;
    fd_ = -1;
    return result;
  }
  void Reset(int fd = -1) {
    if (fd_ >= 0)
      close(fd_);
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

[[noreturn]] void ThrowErrno(const std::string &operation,
                             std::string_view relative_path = {}) {
  const int error = errno;
  std::string message = operation;
  if (!relative_path.empty())
    message += ": " + std::string(relative_path);
  throw std::filesystem::filesystem_error(
      message, std::error_code(error, std::generic_category()));
}

UniqueFd DuplicateDirectoryFd(int source) {
#if defined(F_DUPFD_CLOEXEC)
  const int duplicate = fcntl(source, F_DUPFD_CLOEXEC, 0);
#else
  const int duplicate = dup(source);
#endif
  if (duplicate < 0)
    ThrowErrno("Could not duplicate updater root handle");
  return UniqueFd(duplicate);
}

UniqueFd OpenDirectoryChain(int root,
                            const std::vector<std::string> &components,
                            size_t component_count, bool create,
                            bool missing_ok, mode_t create_mode = 0755,
                            bool require_private = false) {
  UniqueFd current = DuplicateDirectoryFd(root);
  for (size_t index = 0; index < component_count; ++index) {
    bool created = false;
    int next = openat(current.Get(), components[index].c_str(),
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create) {
      if (mkdirat(current.Get(), components[index].c_str(), create_mode) < 0) {
        if (errno != EEXIST)
          ThrowErrno("Could not create updater directory", components[index]);
      } else {
        created = true;
      }
      next = openat(current.Get(), components[index].c_str(),
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    if (next < 0) {
      if (missing_ok && errno == ENOENT)
        return {};
      ThrowErrno("Could not open updater directory", components[index]);
    }
    struct stat status{};
    if (fstat(next, &status) < 0) {
      const int saved_error = errno;
      close(next);
      errno = saved_error;
      ThrowErrno("Could not inspect updater directory", components[index]);
    }
    if (!S_ISDIR(status.st_mode)) {
      close(next);
      throw std::runtime_error(
          "Updater path contains a non-directory component: " +
          components[index]);
    }
    if (require_private) {
      // The top transaction-state directory remains listable/traversable so a
      // launcher running as the non-root install user can perform the normal
      // journal-absence check after a privileged update. Its files and nested
      // rollback directory remain owner-only.
      const mode_t required_mode =
          index == 0 &&
                  UpdaterPathCollisionKey(components[index]) == ".worr-update"
              ? 0755
              : 0700;
      if (created && fchmod(next, required_mode) < 0) {
        const int saved_error = errno;
        close(next);
        errno = saved_error;
        ThrowErrno("Could not protect updater private directory",
                   components[index]);
      }
      if (!created && (status.st_mode & 0777) != required_mode) {
        close(next);
        throw std::runtime_error(
            "Updater private directory permissions are not protected: " +
            components[index]);
      }
      if (created && fsync(next) < 0) {
        const int saved_error = errno;
        close(next);
        errno = saved_error;
        ThrowErrno("Could not flush updater private directory",
                   components[index]);
      }
    } else if (created && fchmod(next, create_mode) < 0) {
      const int saved_error = errno;
      close(next);
      errno = saved_error;
      ThrowErrno("Could not set deterministic updater directory permissions",
                 components[index]);
    } else if (created && fsync(next) < 0) {
      const int saved_error = errno;
      close(next);
      errno = saved_error;
      ThrowErrno("Could not flush updater directory permissions",
                 components[index]);
    }
    if (created && fsync(current.Get()) < 0) {
      const int saved_error = errno;
      close(next);
      errno = saved_error;
      ThrowErrno("Could not flush updater parent directory", components[index]);
    }
    current.Reset(next);
  }
  return current;
}

UniqueFd OpenRegularFile(int parent, std::string_view leaf, bool missing_ok,
                         std::string_view display_path,
                         mode_t *mode = nullptr) {
  const std::string leaf_string(leaf);
  const int fd = openat(parent, leaf_string.c_str(),
                        O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    if (missing_ok && errno == ENOENT)
      return {};
    ThrowErrno("Could not open updater file", display_path);
  }

  UniqueFd file(fd);
  struct stat status{};
  if (fstat(file.Get(), &status) < 0)
    ThrowErrno("Could not inspect updater file", display_path);
  if (!S_ISREG(status.st_mode))
    throw std::runtime_error("Updater path is not a regular file: " +
                             std::string(display_path));
  if (status.st_nlink > 1)
    throw std::runtime_error("Updater file has hard-link aliases: " +
                             std::string(display_path));
  if (mode)
    *mode = status.st_mode & 0777;
  return file;
}

void RequirePrivateFileMode(int file, std::string_view display_path) {
  struct stat status{};
  if (fstat(file, &status) < 0)
    ThrowErrno("Could not inspect private updater file", display_path);
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1 ||
      (status.st_mode & 0777) != 0600 || status.st_uid != geteuid())
    throw std::runtime_error(
        "Private updater file has unsafe ownership or permissions: " +
        std::string(display_path));
}

UniqueFd CreateTemporaryFile(int parent, std::string *leaf) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    *leaf = TemporaryLeafName();
    const int fd =
        openat(parent, leaf->c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0)
      return UniqueFd(fd);
    if (errno != EEXIST)
      ThrowErrno("Could not create updater temporary file", *leaf);
  }
  throw std::runtime_error(
      "Could not allocate a unique updater temporary file");
}

void WriteAll(const void *data, size_t size, int destination) {
  const auto *cursor = static_cast<const unsigned char *>(data);
  while (size != 0) {
    const ssize_t written = write(destination, cursor, size);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      ThrowErrno("Could not write updater temporary file");
    }
    if (written == 0)
      throw std::runtime_error("Updater temporary file write made no progress");
    cursor += written;
    size -= static_cast<size_t>(written);
  }
}

void CopyFileContents(int source, int destination,
                      CopiedFileMeasurement *measurement) {
  std::array<unsigned char, kCopyBufferSize> buffer{};
  for (;;) {
    const ssize_t bytes = read(source, buffer.data(), buffer.size());
    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      ThrowErrno("Could not read updater source file");
    }
    if (bytes == 0)
      return;
    MeasureCopiedBytes(measurement, buffer.data(), static_cast<size_t>(bytes));
    WriteAll(buffer.data(), static_cast<size_t>(bytes), destination);
  }
}

CopiedFileMeasurement MeasureFileContents(int source) {
  CopiedFileMeasurement measurement;
  std::array<unsigned char, kCopyBufferSize> buffer{};
  for (;;) {
    const ssize_t bytes = read(source, buffer.data(), buffer.size());
    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      ThrowErrno("Could not read updater source file");
    }
    if (bytes == 0)
      return measurement;
    MeasureCopiedBytes(&measurement, buffer.data(), static_cast<size_t>(bytes));
  }
}

std::string ReadBoundedFile(int source, size_t maximum_size,
                            std::string_view relative_path) {
  std::string contents;
  std::array<char, kCopyBufferSize> buffer{};
  for (;;) {
    const ssize_t bytes = read(source, buffer.data(), buffer.size());
    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      ThrowErrno("Could not read updater file", relative_path);
    }
    if (bytes == 0)
      return contents;
    const size_t count = static_cast<size_t>(bytes);
    if (contents.size() > maximum_size ||
        count > maximum_size - contents.size())
      throw std::runtime_error("Updater file exceeds its maximum size: " +
                               std::string(relative_path));
    contents.append(buffer.data(), count);
  }
}

#endif

} // namespace

InstallFileExpectation MakeInstallFileExpectation(uint64_t size,
                                                  std::string_view sha256,
                                                  uint32_t mode) {
  if (sha256.size() != 64)
    throw std::runtime_error("Invalid SHA-256 digest length");
  InstallFileExpectation expectation;
  if (mode != 0 && mode != 0644 && mode != 0755)
    throw std::runtime_error("Invalid updater file permission mode");
  expectation.size = size;
  expectation.mode = mode;
  for (size_t index = 0; index < expectation.sha256.size(); ++index) {
    expectation.sha256[index] =
        static_cast<uint8_t>((ParseHexDigit(sha256[index * 2]) << 4U) |
                             ParseHexDigit(sha256[index * 2 + 1]));
  }
  return expectation;
}

#if defined(_WIN32)
bool ProtectedElevationAceGrantsDangerousAccess(
    uint32_t access_mask, uint8_t ace_flags, bool directory,
    uint32_t dangerous_for_object, bool validate_child_inheritance) {
  const auto mapped = [](ACCESS_MASK mask) {
    GENERIC_MAPPING file_mapping = {
        FILE_GENERIC_READ,
        FILE_GENERIC_WRITE,
        FILE_GENERIC_EXECUTE,
        FILE_ALL_ACCESS,
    };
    MapGenericMask(&mask, &file_mapping);
    return mask;
  };

  if ((ace_flags & INHERIT_ONLY_ACE) == 0 &&
      (mapped(access_mask) & mapped(dangerous_for_object)) != 0)
    return true;

  if (!directory || !validate_child_inheritance ||
      (ace_flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) == 0)
    return false;

  // OI and CI ACEs can grant control over files or directories that the
  // privileged updater creates below an otherwise protected root. The FILE_*
  // write bits intentionally cover both aliases: FILE_WRITE_DATA is
  // FILE_ADD_FILE for a directory, and FILE_APPEND_DATA is
  // FILE_ADD_SUBDIRECTORY. NO_PROPAGATE_INHERIT still reaches an immediate
  // child, so it does not make such an ACE safe.
  constexpr ACCESS_MASK dangerous_for_child =
      FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD | DELETE |
      WRITE_DAC | WRITE_OWNER;
  return (mapped(access_mask) & dangerous_for_child) != 0;
}
#endif

void ValidateProtectedElevationBoundary(
    const std::filesystem::path &canonical_root,
    std::string_view worker_relative_path) {
#if defined(_WIN32)
  struct LocalSecurityDescriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~LocalSecurityDescriptor() {
      if (value)
        LocalFree(value);
    }
  };

  std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_sid{};
  std::array<unsigned char, SECURITY_MAX_SID_SIZE> admin_sid{};
  DWORD system_sid_size = static_cast<DWORD>(system_sid.size());
  DWORD admin_sid_size = static_cast<DWORD>(admin_sid.size());
  if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid.data(),
                          &system_sid_size) ||
      !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
                          admin_sid.data(), &admin_sid_size))
    ThrowWindowsError(GetLastError(),
                      "Could not construct protected-root identities");

  DWORD trusted_installer_sid_size = 0;
  DWORD trusted_installer_domain_size = 0;
  SID_NAME_USE trusted_installer_use{};
  (void)LookupAccountNameW(nullptr, L"NT SERVICE\\TrustedInstaller", nullptr,
                           &trusted_installer_sid_size, nullptr,
                           &trusted_installer_domain_size,
                           &trusted_installer_use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
      trusted_installer_sid_size == 0)
    ThrowWindowsError(GetLastError(),
                      "Could not resolve the TrustedInstaller identity");
  std::vector<unsigned char> trusted_installer_sid(
      trusted_installer_sid_size);
  std::vector<wchar_t> trusted_installer_domain(
      trusted_installer_domain_size);
  if (!LookupAccountNameW(
          nullptr, L"NT SERVICE\\TrustedInstaller",
          trusted_installer_sid.data(), &trusted_installer_sid_size,
          trusted_installer_domain.data(), &trusted_installer_domain_size,
          &trusted_installer_use))
    ThrowWindowsError(GetLastError(),
                      "Could not resolve the TrustedInstaller identity");

  const std::array<PSID, 3> trusted_sids = {
      system_sid.data(), admin_sid.data(), trusted_installer_sid.data()};
  const auto trusted_sid = [&trusted_sids](PSID sid) {
    return std::any_of(trusted_sids.begin(), trusted_sids.end(),
                       [sid](PSID trusted) {
                         return EqualSid(sid, trusted) != FALSE;
                       });
  };
  const auto validate_path = [&trusted_sid](const std::filesystem::path &path,
                                            bool directory,
                                            ACCESS_MASK dangerous,
                                            bool validate_child_inheritance) {
    const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT |
                        (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
    UniqueHandle object(CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, flags, nullptr));
    if (!object)
      ThrowWindowsError(GetLastError(),
                        "Could not inspect protected elevation path",
                        path.generic_u8string());
    if (directory)
      ValidateDirectoryHandle(object.Get(), path.generic_u8string());
    else
      ValidateRegularFileHandle(object.Get(), path.generic_u8string());

    PSID owner = nullptr;
    PACL acl = nullptr;
    LocalSecurityDescriptor descriptor;
    const DWORD result = GetSecurityInfo(
        object.Get(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
        nullptr, &acl, nullptr, &descriptor.value);
    if (result != ERROR_SUCCESS)
      ThrowWindowsError(result,
                        "Could not read protected elevation permissions",
                        path.generic_u8string());
    if (!owner || !trusted_sid(owner) || !acl)
      throw std::runtime_error(
          "Protected elevation path has an untrusted owner or null DACL: " +
          path.generic_u8string());

    for (DWORD index = 0; index < acl->AceCount; ++index) {
      void *raw_ace = nullptr;
      if (!GetAce(acl, index, &raw_ace))
        ThrowWindowsError(GetLastError(),
                          "Could not inspect protected elevation ACE",
                          path.generic_u8string());
      const auto *header = static_cast<const ACE_HEADER *>(raw_ace);
      if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
        if (header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
            header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
            header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE) {
          const auto *mask = reinterpret_cast<const ACCESS_MASK *>(
              static_cast<const unsigned char *>(raw_ace) +
              sizeof(ACE_HEADER));
          if (ProtectedElevationAceGrantsDangerousAccess(
                  *mask, header->AceFlags, directory, dangerous,
                  validate_child_inheritance))
            throw std::runtime_error(
                "Protected elevation path has an unsupported writable ACE: " +
                path.generic_u8string());
        }
        continue;
      }
      const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw_ace);
      const PSID sid = const_cast<DWORD *>(&ace->SidStart);
      if (!trusted_sid(sid) &&
          ProtectedElevationAceGrantsDangerousAccess(
              ace->Mask, header->AceFlags, directory, dangerous,
              validate_child_inheritance))
        throw std::runtime_error(
            "Protected elevation path is writable by an untrusted identity: " +
            path.generic_u8string());
    }
  };

  const std::filesystem::path root = std::filesystem::canonical(canonical_root);
  validate_path(root, true,
                FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD |
                    DELETE | WRITE_DAC | WRITE_OWNER,
                true);
  std::filesystem::path ancestor = root.parent_path();
  while (!ancestor.empty() && ancestor != ancestor.parent_path()) {
    validate_path(ancestor, true,
                  FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER,
                  false);
    ancestor = ancestor.parent_path();
  }
  if (!ancestor.empty())
    validate_path(ancestor, true,
                  FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER,
                  false);

  const std::vector<std::string> worker_components =
      SplitUpdaterPath(worker_relative_path);
  std::filesystem::path worker_parent = root;
  for (size_t index = 0; index + 1 < worker_components.size(); ++index) {
    worker_parent /= std::filesystem::u8path(worker_components[index]);
    validate_path(worker_parent, true,
                  FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD |
                      DELETE | WRITE_DAC | WRITE_OWNER,
                  true);
  }
  validate_path(root / std::filesystem::u8path(
                           CanonicalUpdaterRelativePath(worker_relative_path)),
                 false,
                 FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC |
                     WRITE_OWNER,
                 false);
#else
  (void)canonical_root;
  (void)worker_relative_path;
  throw std::runtime_error(
      "Protected elevation boundary validation is only available on Windows");
#endif
}

struct AnchoredFileTree::Impl {
  InstallFileOperationHook operation_hook;
#if defined(_WIN32)
  UniqueHandle root;
#else
  UniqueFd root;
#endif
};

struct AnchoredFileLock::Impl {
#if defined(_WIN32)
  UniqueHandle file;
#else
  UniqueFd file;
#endif
};

AnchoredFileLock::AnchoredFileLock(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
AnchoredFileLock::~AnchoredFileLock() = default;
AnchoredFileLock::AnchoredFileLock(AnchoredFileLock &&) noexcept = default;
AnchoredFileLock &
AnchoredFileLock::operator=(AnchoredFileLock &&) noexcept = default;

AnchoredFileTree::AnchoredFileTree(const std::filesystem::path &root,
                                   InstallFileOperationHook operation_hook)
    : impl_(std::make_unique<Impl>()) {
  impl_->operation_hook = std::move(operation_hook);
#if defined(_WIN32)
  const HANDLE handle = CreateFileW(
      root.c_str(),
      FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    ThrowWindowsError(GetLastError(), "Could not anchor updater root",
                      root.u8string());
  impl_->root.Reset(handle);
  ValidateDirectoryHandle(impl_->root.Get(), root.u8string());
#else
  const int fd =
      open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    ThrowErrno("Could not anchor updater root", root.string());
  impl_->root.Reset(fd);
#endif
}

AnchoredFileTree::~AnchoredFileTree() = default;
AnchoredFileTree::AnchoredFileTree(AnchoredFileTree &&) noexcept = default;
AnchoredFileTree &
AnchoredFileTree::operator=(AnchoredFileTree &&) noexcept = default;

void AnchoredFileTree::NotifyOperation(
    InstallFileOperationCheckpoint checkpoint,
    std::string_view relative_path) const {
  if (impl_->operation_hook)
    impl_->operation_hook(checkpoint, relative_path);
}

bool AnchoredFileTree::HasRegularFile(std::string_view relative_path) const {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, true);
  if (!parent)
    return false;
  return static_cast<bool>(OpenRegularFile(parent.Get(), components.back(),
                                           GENERIC_READ, true, relative_path));
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, true);
  if (!parent)
    return false;
  return static_cast<bool>(
      OpenRegularFile(parent.Get(), components.back(), true, relative_path));
#endif
}

bool AnchoredFileTree::FileMatches(
    std::string_view relative_path,
    const InstallFileExpectation &expectation) const {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, true);
  if (!parent)
    return false;
  UniqueHandle file = OpenRegularFile(parent.Get(), components.back(),
                                      GENERIC_READ, true, relative_path);
  if (!file)
    return false;
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file.Get(), &file_size))
    ThrowWindowsError(GetLastError(), "Could not size updater file",
                      relative_path);
  if (file_size.QuadPart < 0 ||
      static_cast<uint64_t>(file_size.QuadPart) != expectation.size)
    return false;
  CopiedFileMeasurement measurement;
  if (expectation.size != 0)
    measurement = MeasureFileContents(file.Get());
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, true);
  if (!parent)
    return false;
  mode_t file_mode = 0;
  UniqueFd file = OpenRegularFile(parent.Get(), components.back(), true,
                                  relative_path, &file_mode);
  if (!file)
    return false;
  CopiedFileMeasurement measurement = MeasureFileContents(file.Get());
  if (expectation.mode != 0 && file_mode != expectation.mode)
    return false;
#endif
  if (measurement.size != expectation.size)
    return false;
  return measurement.sha256.Final() == expectation.sha256;
}

std::string AnchoredFileTree::ReadFile(std::string_view relative_path,
                                       std::size_t maximum_size) const {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, false);
  const bool private_state = IsPrivateUpdaterStatePath(relative_path);
  UniqueHandle file = OpenRegularFile(
      parent.Get(), components.back(),
      GENERIC_READ | (private_state ? READ_CONTROL : 0), false, relative_path,
      private_state ? FILE_SHARE_READ
                    : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  if (private_state)
    RequirePrivateFileAcl(file.Get(), relative_path);
  return ReadBoundedFile(file.Get(), maximum_size, relative_path);
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, false);
  UniqueFd file =
      OpenRegularFile(parent.Get(), components.back(), false, relative_path);
  if (IsPrivateUpdaterStatePath(relative_path))
    RequirePrivateFileMode(file.Get(), relative_path);
  return ReadBoundedFile(file.Get(), maximum_size, relative_path);
#endif
}

void AnchoredFileTree::EnsurePrivateDirectory(std::string_view relative_path) {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  (void)OpenDirectoryChain(impl_->root.Get(), components, components.size(),
                           true, false, true);
#else
  (void)OpenDirectoryChain(impl_->root.Get(), components, components.size(),
                           true, false, 0700, true);
#endif
}

AnchoredFileLock
AnchoredFileTree::AcquireExclusiveLock(std::string_view relative_path) {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
  auto lock = std::make_unique<AnchoredFileLock::Impl>();
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, true, false);
  lock->file =
      NativeOpen(parent.Get(), components.back(),
                 GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES |
                     READ_CONTROL | WRITE_DAC | WRITE_OWNER | SYNCHRONIZE,
                 FILE_OPEN_IF, FILE_NON_DIRECTORY_FILE, false, FILE_SHARE_READ);
  ValidateRegularFileHandle(lock->file.Get(), relative_path);
  ValidatePrivateFileAcl(lock->file.Get(), relative_path);
  OVERLAPPED overlapped{};
  if (!LockFileEx(lock->file.Get(),
                  LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                  MAXDWORD, MAXDWORD, &overlapped)) {
    const DWORD error = GetLastError();
    if (error == ERROR_LOCK_VIOLATION)
      throw std::runtime_error("Another updater transaction is active");
    ThrowWindowsError(error, "Could not lock updater transaction",
                      relative_path);
  }
#else
  UniqueFd parent =
      OpenDirectoryChain(impl_->root.Get(), components, components.size() - 1,
                         true, false, 0700, true);
  const std::string &leaf = components.back();
  const int fd = openat(parent.Get(), leaf.c_str(),
                        O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    ThrowErrno("Could not open updater transaction lock", relative_path);
  lock->file.Reset(fd);
  struct stat status{};
  if (fstat(lock->file.Get(), &status) < 0)
    ThrowErrno("Could not inspect updater transaction lock", relative_path);
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
    throw std::runtime_error(
        "Updater transaction lock must be a single-link regular file");
  if (fchmod(lock->file.Get(), 0600) < 0)
    ThrowErrno("Could not protect updater transaction lock", relative_path);
  if (flock(lock->file.Get(), LOCK_EX | LOCK_NB) < 0) {
    if (errno == EWOULDBLOCK)
      throw std::runtime_error("Another updater transaction is active");
    ThrowErrno("Could not lock updater transaction", relative_path);
  }
#endif
  return AnchoredFileLock(std::move(lock));
}

AnchoredFileLock AnchoredFileTree::AcquireExistingFileLock(
    std::string_view relative_path, bool exclusive) {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
  auto lock = std::make_unique<AnchoredFileLock::Impl>();
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, false);
  lock->file = OpenRegularFile(parent.Get(), components.back(), GENERIC_READ,
                               false, relative_path, FILE_SHARE_READ);
  OVERLAPPED overlapped{};
  const DWORD flags = LOCKFILE_FAIL_IMMEDIATELY |
                      (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0);
  if (!LockFileEx(lock->file.Get(), flags, 0, MAXDWORD, MAXDWORD,
                  &overlapped)) {
    const DWORD error = GetLastError();
    if (error == ERROR_LOCK_VIOLATION)
      throw std::runtime_error(
          "Updater-managed file is locked by another process");
    ThrowWindowsError(error, "Could not lock updater-managed file",
                      relative_path);
  }
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, false);
  lock->file =
      OpenRegularFile(parent.Get(), components.back(), false, relative_path);
  if (flock(lock->file.Get(), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0) {
    if (errno == EWOULDBLOCK)
      throw std::runtime_error(
          "Updater-managed file is locked by another process");
    ThrowErrno("Could not lock updater-managed file", relative_path);
  }
#endif
  return AnchoredFileLock(std::move(lock));
}

AnchoredFileLock AnchoredFileTree::AcquireExistingFileSharedLock(
    std::string_view relative_path) {
  return AcquireExistingFileLock(relative_path, false);
}

AnchoredFileLock AnchoredFileTree::AcquireExistingFileExclusiveLock(
    std::string_view relative_path) {
  return AcquireExistingFileLock(relative_path, true);
}

bool AnchoredFileTree::ReplaceFileFrom(
    const AnchoredFileTree &source, std::string_view relative_path,
    const InstallFileExpectation *expectation, uint32_t *copied_source_mode,
    const uint32_t *destination_mode) {
  return ReplaceFileFrom(source, relative_path, relative_path, expectation,
                         copied_source_mode, destination_mode);
}

InstallPathKind AnchoredFileTree::GetPathKind(
    std::string_view relative_path, uint32_t *mode) const {
  if (mode)
    *mode = 0;
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, true);
  if (!parent)
    return InstallPathKind::Missing;
  UniqueHandle leaf =
      NativeOpen(parent.Get(), components.back(),
                 FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN, 0, true);
  if (!leaf)
    return InstallPathKind::Missing;
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(leaf.Get(), &information))
    ThrowWindowsError(GetLastError(), "Could not inspect updater path",
                      relative_path);
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    throw std::runtime_error("Updater path is a reparse point: " +
                             std::string(relative_path));
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    return InstallPathKind::Directory;
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0)
    throw std::runtime_error("Updater path is not a regular file or directory: " +
                             std::string(relative_path));
  if (information.nNumberOfLinks > 1)
    throw std::runtime_error("Updater file has hard-link aliases: " +
                             std::string(relative_path));
  return InstallPathKind::RegularFile;
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, true);
  if (!parent)
    return InstallPathKind::Missing;
  struct stat status{};
  if (fstatat(parent.Get(), components.back().c_str(), &status,
              AT_SYMLINK_NOFOLLOW) < 0) {
    if (errno == ENOENT)
      return InstallPathKind::Missing;
    ThrowErrno("Could not inspect updater path", relative_path);
  }
  if (mode)
    *mode = static_cast<uint32_t>(status.st_mode & 0777);
  if (S_ISDIR(status.st_mode))
    return InstallPathKind::Directory;
  if (!S_ISREG(status.st_mode))
    throw std::runtime_error("Updater path is not a regular file or directory: " +
                             std::string(relative_path));
  if (status.st_nlink > 1)
    throw std::runtime_error("Updater file has hard-link aliases: " +
                             std::string(relative_path));
  return InstallPathKind::RegularFile;
#endif
}

bool AnchoredFileTree::EnsureDirectory(std::string_view relative_path,
                                       uint32_t mode) {
  if ((mode & ~uint32_t{0777}) != 0)
    throw std::runtime_error("Updater directory mode contains unsupported bits");
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, false);
  bool created = false;
  UniqueHandle directory;
  try {
    directory = NativeOpen(
        parent.Get(), components.back(),
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
            SYNCHRONIZE,
        FILE_CREATE, FILE_DIRECTORY_FILE, false);
    created = true;
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_FILE_EXISTS &&
        error.code().value() != ERROR_ALREADY_EXISTS)
      throw;
    directory = NativeOpen(
        parent.Get(), components.back(),
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
            SYNCHRONIZE,
        FILE_OPEN, FILE_DIRECTORY_FILE, false);
  }
  ValidateDirectoryHandle(directory.Get(), relative_path);
  return created;
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, false);
  const std::string &leaf = components.back();
  bool created = false;
  if (mkdirat(parent.Get(), leaf.c_str(), static_cast<mode_t>(mode)) < 0) {
    if (errno != EEXIST)
      ThrowErrno("Could not create updater directory", relative_path);
  } else {
    created = true;
  }
  UniqueFd directory = OpenDirectoryChain(parent.Get(), {leaf}, 1, false,
                                          false);
  if (created) {
    if (fchmod(directory.Get(), static_cast<mode_t>(mode)) < 0)
      ThrowErrno("Could not set updater directory permissions", relative_path);
    if (fsync(directory.Get()) < 0)
      ThrowErrno("Could not flush updater directory", relative_path);
    if (fsync(parent.Get()) < 0)
      ThrowErrno("Could not flush updater parent directory", relative_path);
    NotifyOperation(InstallFileOperationCheckpoint::AfterParentDirectoryFlushed,
                    relative_path);
  }
  return created;
#endif
}

std::vector<InstallDirectoryEntry>
AnchoredFileTree::ListDirectory(std::string_view relative_path,
                                std::size_t maximum_entries) const {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
  std::vector<InstallDirectoryEntry> entries;
#if defined(_WIN32)
  UniqueHandle directory = OpenDirectoryChain(
      impl_->root.Get(), components, components.size(), false, false);
  alignas(FILE_ID_BOTH_DIR_INFO) std::array<unsigned char, 64 * 1024> storage{};
  bool restart = true;
  for (;;) {
    const FILE_INFO_BY_HANDLE_CLASS information_class =
        restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
    if (!GetFileInformationByHandleEx(directory.Get(), information_class,
                                      storage.data(),
                                      static_cast<DWORD>(storage.size()))) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_FILES)
        break;
      ThrowWindowsError(error, "Could not enumerate updater directory",
                        relative_path);
    }
    restart = false;
    const auto *information =
        reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(storage.data());
    for (;;) {
      const std::wstring native_name(
          information->FileName,
          information->FileNameLength / sizeof(wchar_t));
      if (native_name != L"." && native_name != L"..") {
        const std::string name = std::filesystem::path(native_name).u8string();
        const std::string full_path =
            std::string(relative_path) + "/" + name;
        if (!IsSafeUpdaterRelativePath(full_path))
          throw std::runtime_error(
              "Updater directory contains a non-portable entry: " +
              full_path);
        InstallDirectoryEntry entry;
        entry.name = name;
        if ((information->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
          throw std::runtime_error("Updater directory contains a reparse point: " +
                                   full_path);
        if ((information->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
          UniqueHandle child = NativeOpen(
              directory.Get(), name, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
              FILE_OPEN, FILE_DIRECTORY_FILE, false);
          ValidateDirectoryHandle(child.Get(), full_path);
          entry.kind = InstallPathKind::Directory;
        } else {
          UniqueHandle child = OpenRegularFile(
              directory.Get(), name, FILE_READ_ATTRIBUTES, false, full_path);
          entry.kind = InstallPathKind::RegularFile;
        }
        if (entries.size() >= maximum_entries)
          throw std::runtime_error(
              "Updater directory contains too many entries: " +
              std::string(relative_path));
        entries.push_back(std::move(entry));
      }
      if (information->NextEntryOffset == 0)
        break;
      information = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(
          reinterpret_cast<const unsigned char *>(information) +
          information->NextEntryOffset);
    }
  }
#else
  UniqueFd directory = OpenDirectoryChain(
      impl_->root.Get(), components, components.size(), false, false);
  DIR *raw_directory = fdopendir(directory.Release());
  if (!raw_directory)
    ThrowErrno("Could not enumerate updater directory", relative_path);
  const std::unique_ptr<DIR, int (*)(DIR *)> stream(raw_directory, closedir);
  errno = 0;
  while (dirent *entry = readdir(stream.get())) {
    const std::string name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    const std::string full_path = std::string(relative_path) + "/" + name;
    if (!IsSafeUpdaterRelativePath(full_path))
      throw std::runtime_error(
          "Updater directory contains a non-portable entry: " + full_path);
    struct stat status{};
    if (fstatat(dirfd(stream.get()), name.c_str(), &status,
                AT_SYMLINK_NOFOLLOW) < 0)
      ThrowErrno("Could not inspect updater directory entry", full_path);
    InstallDirectoryEntry listed;
    listed.name = name;
    listed.mode = static_cast<uint32_t>(status.st_mode & 0777);
    if (S_ISDIR(status.st_mode)) {
      listed.kind = InstallPathKind::Directory;
    } else if (S_ISREG(status.st_mode)) {
      if (status.st_nlink > 1)
        throw std::runtime_error("Updater file has hard-link aliases: " +
                                 full_path);
      listed.kind = InstallPathKind::RegularFile;
    } else {
      throw std::runtime_error(
          "Updater directory contains a link or special entry: " + full_path);
    }
    if (entries.size() >= maximum_entries)
      throw std::runtime_error(
          "Updater directory contains too many entries: " +
          std::string(relative_path));
    entries.push_back(std::move(listed));
    errno = 0;
  }
  if (errno != 0)
    ThrowErrno("Could not enumerate updater directory", relative_path);
#endif
  std::sort(entries.begin(), entries.end(),
            [](const InstallDirectoryEntry &left,
               const InstallDirectoryEntry &right) {
              const std::string left_key = UpdaterPathCollisionKey(left.name);
              const std::string right_key = UpdaterPathCollisionKey(right.name);
              return left_key == right_key ? left.name < right.name
                                           : left_key < right_key;
            });
  for (std::size_t index = 1; index < entries.size(); ++index) {
    if (UpdaterPathCollisionKey(entries[index - 1].name) ==
        UpdaterPathCollisionKey(entries[index].name))
      throw std::runtime_error(
          "Updater directory contains colliding portable names: " +
          entries[index - 1].name + " and " + entries[index].name);
  }
  return entries;
}

bool AnchoredFileTree::ReplaceFileFrom(
    const AnchoredFileTree &source, std::string_view source_path,
    std::string_view destination_path,
    const InstallFileExpectation *expectation, uint32_t *copied_source_mode,
    const uint32_t *destination_mode) {
  if (destination_mode && (*destination_mode & ~uint32_t{0777}) != 0)
    throw std::runtime_error("Updater destination mode contains unsupported bits");
  const std::vector<std::string> source_components =
      SplitUpdaterPath(source_path);
  const std::vector<std::string> destination_components =
      SplitUpdaterPath(destination_path);

#if defined(_WIN32)
  UniqueHandle source_parent =
      OpenDirectoryChain(source.impl_->root.Get(), source_components,
                         source_components.size() - 1, false, true);
  if (!source_parent)
    return false;
  const bool private_source = IsPrivateUpdaterStatePath(source_path);
  UniqueHandle source_file = OpenRegularFile(
      source_parent.Get(), source_components.back(),
      GENERIC_READ | (private_source ? READ_CONTROL : 0), true, source_path,
      private_source ? FILE_SHARE_READ
                     : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  if (!source_file)
    return false;
  if (private_source)
    RequirePrivateFileAcl(source_file.Get(), source_path);
  if (copied_source_mode)
    *copied_source_mode = 0;

  UniqueHandle destination_parent =
      OpenDirectoryChain(impl_->root.Get(), destination_components,
                         destination_components.size() - 1, true, false);
  (void)OpenRegularFile(destination_parent.Get(), destination_components.back(),
                        FILE_READ_ATTRIBUTES, true, destination_path);

  std::string temporary_leaf;
  const bool private_state = IsPrivateUpdaterStatePath(destination_path);
  UniqueHandle temporary = CreateTemporaryFile(destination_parent.Get(),
                                               &temporary_leaf, private_state);
  if (private_state)
    ValidatePrivateFileAcl(temporary.Get(), destination_path);
  CopiedFileMeasurement measurement;
  try {
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileCreated,
                    destination_path);
    CopyFileContents(source_file.Get(), temporary.Get(),
                     expectation ? &measurement : nullptr);
    ValidateCopiedFile(expectation ? &measurement : nullptr, expectation,
                       source_path);
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileWritten,
                    destination_path);
    if (!FlushFileBuffers(temporary.Get()))
      ThrowWindowsError(GetLastError(),
                        "Could not flush updater temporary file",
                        destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::BeforeAtomicReplacement,
                    destination_path);
    RenameTemporaryFile(temporary.Get(), destination_parent.Get(),
                        destination_components.back());
  } catch (...) {
    DeleteOpenFile(temporary.Get());
    throw;
  }
#else
  UniqueFd source_parent =
      OpenDirectoryChain(source.impl_->root.Get(), source_components,
                         source_components.size() - 1, false, true);
  if (!source_parent)
    return false;
  mode_t source_mode = 0;
  UniqueFd source_file =
      OpenRegularFile(source_parent.Get(), source_components.back(), true,
                      source_path, &source_mode);
  if (!source_file)
    return false;
  if (IsPrivateUpdaterStatePath(source_path))
    RequirePrivateFileMode(source_file.Get(), source_path);
  if (copied_source_mode)
    *copied_source_mode = static_cast<uint32_t>(source_mode);

  UniqueFd destination_parent =
      OpenDirectoryChain(impl_->root.Get(), destination_components,
                         destination_components.size() - 1, true, false);
  (void)OpenRegularFile(destination_parent.Get(), destination_components.back(),
                        true, destination_path);

  std::string temporary_leaf;
  UniqueFd temporary =
      CreateTemporaryFile(destination_parent.Get(), &temporary_leaf);
  CopiedFileMeasurement measurement;
  try {
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileCreated,
                    destination_path);
    CopyFileContents(source_file.Get(), temporary.Get(),
                     expectation ? &measurement : nullptr);
    ValidateCopiedFile(expectation ? &measurement : nullptr, expectation,
                       source_path);
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileWritten,
                    destination_path);
    const mode_t target_mode =
        IsPrivateUpdaterStatePath(destination_path)
            ? 0600
            : destination_mode ? static_cast<mode_t>(*destination_mode)
            : expectation && expectation->mode != 0
                ? static_cast<mode_t>(expectation->mode)
                : source_mode;
    if (fchmod(temporary.Get(), target_mode) < 0)
      ThrowErrno("Could not preserve updater file permissions",
                 destination_path);
    if (fsync(temporary.Get()) < 0)
      ThrowErrno("Could not flush updater temporary file", destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::BeforeAtomicReplacement,
                    destination_path);
    if (renameat(destination_parent.Get(), temporary_leaf.c_str(),
                 destination_parent.Get(),
                 destination_components.back().c_str()) < 0)
      ThrowErrno("Could not atomically replace updater file", destination_path);
    if (fsync(destination_parent.Get()) < 0)
      ThrowErrno("Could not flush updater directory", destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::AfterParentDirectoryFlushed,
                    destination_path);
  } catch (...) {
    unlinkat(destination_parent.Get(), temporary_leaf.c_str(), 0);
    throw;
  }
#endif
  return true;
}

void AnchoredFileTree::ReplaceFileWithBytes(std::string_view destination_path,
                                            const std::string &contents,
                                            InstallFileWriteOptions options) {
  if ((options.mode & ~uint32_t{0777}) != 0)
    throw std::runtime_error("Updater file mode contains unsupported bits");
  const std::vector<std::string> components =
      SplitUpdaterPath(destination_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, true, false);
  (void)OpenRegularFile(parent.Get(), components.back(), FILE_READ_ATTRIBUTES,
                        true, destination_path);
  std::string temporary_leaf;
  const bool private_state = IsPrivateUpdaterStatePath(destination_path);
  UniqueHandle temporary =
      CreateTemporaryFile(parent.Get(), &temporary_leaf, private_state);
  if (private_state)
    ValidatePrivateFileAcl(temporary.Get(), destination_path);
  try {
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileCreated,
                    destination_path);
    WriteAll(contents.data(), contents.size(), temporary.Get());
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileWritten,
                    destination_path);
    if (!FlushFileBuffers(temporary.Get()))
      ThrowWindowsError(GetLastError(),
                        "Could not flush updater temporary file",
                        destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::BeforeAtomicReplacement,
                    destination_path);
    RenameTemporaryFile(temporary.Get(), parent.Get(), components.back());
  } catch (...) {
    DeleteOpenFile(temporary.Get());
    throw;
  }
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, true, false);
  mode_t destination_mode = static_cast<mode_t>(options.mode);
  mode_t existing_mode = 0;
  UniqueFd existing = OpenRegularFile(parent.Get(), components.back(), true,
                                      destination_path, &existing_mode);
  if (existing && options.preserve_existing_mode)
    destination_mode = existing_mode;
  std::string temporary_leaf;
  UniqueFd temporary = CreateTemporaryFile(parent.Get(), &temporary_leaf);
  try {
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileCreated,
                    destination_path);
    WriteAll(contents.data(), contents.size(), temporary.Get());
    NotifyOperation(InstallFileOperationCheckpoint::AfterTemporaryFileWritten,
                    destination_path);
    if (fchmod(temporary.Get(), destination_mode) < 0)
      ThrowErrno("Could not set updater file permissions", destination_path);
    if (fsync(temporary.Get()) < 0)
      ThrowErrno("Could not flush updater temporary file", destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::BeforeAtomicReplacement,
                    destination_path);
    if (renameat(parent.Get(), temporary_leaf.c_str(), parent.Get(),
                 components.back().c_str()) < 0)
      ThrowErrno("Could not atomically replace updater file", destination_path);
    if (fsync(parent.Get()) < 0)
      ThrowErrno("Could not flush updater directory", destination_path);
    NotifyOperation(InstallFileOperationCheckpoint::AfterParentDirectoryFlushed,
                    destination_path);
  } catch (...) {
    unlinkat(parent.Get(), temporary_leaf.c_str(), 0);
    throw;
  }
#endif
}

bool AnchoredFileTree::RemoveFile(std::string_view relative_path) {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, true);
  if (!parent)
    return false;
  UniqueHandle file =
      OpenRegularFile(parent.Get(), components.back(),
                      DELETE | FILE_READ_ATTRIBUTES, true, relative_path);
  if (!file)
    return false;
  NotifyOperation(InstallFileOperationCheckpoint::BeforeRemoval, relative_path);
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  if (!SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition,
                                  sizeof(disposition)))
    ThrowWindowsError(GetLastError(), "Could not remove updater file",
                      relative_path);
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, true);
  if (!parent)
    return false;
  UniqueFd file =
      OpenRegularFile(parent.Get(), components.back(), true, relative_path);
  if (!file)
    return false;
  struct stat opened_status{};
  if (fstat(file.Get(), &opened_status) < 0)
    ThrowErrno("Could not inspect updater file before removal", relative_path);
  NotifyOperation(InstallFileOperationCheckpoint::BeforeRemoval, relative_path);
  std::string quarantine_leaf;
  UniqueFd reservation = CreateTemporaryFile(parent.Get(), &quarantine_leaf);
  if (renameat(parent.Get(), components.back().c_str(), parent.Get(),
               quarantine_leaf.c_str()) < 0)
    ThrowErrno("Could not quarantine updater file before removal",
               relative_path);

  UniqueFd quarantined =
      OpenRegularFile(parent.Get(), quarantine_leaf, false, relative_path);
  struct stat quarantined_status{};
  if (fstat(quarantined.Get(), &quarantined_status) < 0)
    ThrowErrno("Could not inspect quarantined updater file", relative_path);
  if (opened_status.st_dev != quarantined_status.st_dev ||
      opened_status.st_ino != quarantined_status.st_ino) {
    // The leaf was exchanged after it was validated. Never unlink the
    // unexpected replacement. Restore it without overwriting a concurrently
    // recreated destination; if that name is occupied, the quarantined file is
    // deliberately retained for operator/recovery inspection.
    if (linkat(parent.Get(), quarantine_leaf.c_str(), parent.Get(),
               components.back().c_str(), 0) == 0) {
      if (unlinkat(parent.Get(), quarantine_leaf.c_str(), 0) < 0)
        ThrowErrno("Could not remove restored quarantine link", relative_path);
      if (fsync(parent.Get()) < 0)
        ThrowErrno("Could not flush updater directory after leaf recovery",
                   relative_path);
    }
    throw std::runtime_error(
        "Updater file changed concurrently before removal: " +
        std::string(relative_path));
  }
  if (unlinkat(parent.Get(), quarantine_leaf.c_str(), 0) < 0)
    ThrowErrno("Could not remove quarantined updater file", relative_path);
  if (fsync(parent.Get()) < 0)
    ThrowErrno("Could not flush updater directory after removal",
               relative_path);
  NotifyOperation(InstallFileOperationCheckpoint::AfterParentDirectoryFlushed,
                  relative_path);
#endif
  return true;
}

bool AnchoredFileTree::RemoveEmptyDirectory(std::string_view relative_path) {
  const std::vector<std::string> components = SplitUpdaterPath(relative_path);
#if defined(_WIN32)
  UniqueHandle parent = OpenDirectoryChain(impl_->root.Get(), components,
                                           components.size() - 1, false, true);
  if (!parent)
    return false;
  UniqueHandle directory = NativeOpen(
      parent.Get(), components.back(),
      DELETE | FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
          SYNCHRONIZE,
      FILE_OPEN, FILE_DIRECTORY_FILE, true);
  if (!directory)
    return false;
  ValidateDirectoryHandle(directory.Get(), relative_path);
  NotifyOperation(InstallFileOperationCheckpoint::BeforeRemoval, relative_path);
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  if (!SetFileInformationByHandle(directory.Get(), FileDispositionInfo,
                                  &disposition, sizeof(disposition)))
    ThrowWindowsError(GetLastError(),
                      "Could not remove empty updater directory",
                      relative_path);
#else
  UniqueFd parent = OpenDirectoryChain(impl_->root.Get(), components,
                                       components.size() - 1, false, true);
  if (!parent)
    return false;
  const std::string &leaf = components.back();
  const int directory_fd =
      openat(parent.Get(), leaf.c_str(),
             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (directory_fd < 0) {
    if (errno == ENOENT)
      return false;
    ThrowErrno("Could not open updater directory for removal", relative_path);
  }
  UniqueFd directory(directory_fd);
  struct stat opened_status{};
  if (fstat(directory.Get(), &opened_status) < 0)
    ThrowErrno("Could not inspect updater directory before removal",
               relative_path);
  NotifyOperation(InstallFileOperationCheckpoint::BeforeRemoval, relative_path);

  std::string quarantine_leaf;
  bool reserved = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    quarantine_leaf = TemporaryLeafName();
    if (mkdirat(parent.Get(), quarantine_leaf.c_str(), 0700) == 0) {
      reserved = true;
      break;
    }
    if (errno != EEXIST)
      ThrowErrno("Could not reserve updater directory quarantine",
                 relative_path);
  }
  if (!reserved)
    throw std::runtime_error(
        "Could not allocate a unique updater directory quarantine");
  if (renameat(parent.Get(), leaf.c_str(), parent.Get(),
               quarantine_leaf.c_str()) < 0) {
    const int saved_error = errno;
    (void)unlinkat(parent.Get(), quarantine_leaf.c_str(), AT_REMOVEDIR);
    errno = saved_error;
    ThrowErrno("Could not quarantine updater directory before removal",
               relative_path);
  }

  const int quarantined_fd =
      openat(parent.Get(), quarantine_leaf.c_str(),
             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (quarantined_fd < 0)
    ThrowErrno("Could not inspect quarantined updater directory",
               relative_path);
  UniqueFd quarantined(quarantined_fd);
  struct stat quarantined_status{};
  if (fstat(quarantined.Get(), &quarantined_status) < 0)
    ThrowErrno("Could not inspect quarantined updater directory",
               relative_path);
  if (opened_status.st_dev != quarantined_status.st_dev ||
      opened_status.st_ino != quarantined_status.st_ino) {
    if (renameat(parent.Get(), quarantine_leaf.c_str(), parent.Get(),
                 leaf.c_str()) < 0)
      ThrowErrno("Could not restore exchanged updater directory",
                 relative_path);
    if (fsync(parent.Get()) < 0)
      ThrowErrno("Could not flush updater directory after leaf recovery",
                 relative_path);
    throw std::runtime_error(
        "Updater directory changed concurrently before removal: " +
        std::string(relative_path));
  }
  if (unlinkat(parent.Get(), quarantine_leaf.c_str(), AT_REMOVEDIR) < 0) {
    const int removal_error = errno;
    if (renameat(parent.Get(), quarantine_leaf.c_str(), parent.Get(),
                 leaf.c_str()) < 0)
      ThrowErrno("Could not restore non-empty updater directory",
                 relative_path);
    errno = removal_error;
    ThrowErrno("Could not remove empty updater directory", relative_path);
  }
  if (fsync(parent.Get()) < 0)
    ThrowErrno("Could not flush updater directory after removal",
               relative_path);
  NotifyOperation(InstallFileOperationCheckpoint::AfterParentDirectoryFlushed,
                  relative_path);
#endif
  return true;
}

} // namespace worr::updater
