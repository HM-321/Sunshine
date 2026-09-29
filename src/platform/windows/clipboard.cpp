/**
 * @file src/platform/windows/clipboard.cpp
 * @brief Text-only Windows clipboard integration.
 */

#include "clipboard.h"

#include "logging.h"
#include "utf_utils.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace platf::clipboard {
  namespace {
    std::mutex clipboard_mutex;
    bool ignore_next_update = false;

    class clipboard_guard_t {
    public:
      clipboard_guard_t() = default;

      bool open() {
        // The clipboard can temporarily be owned by another application.
        // Retry briefly instead of failing on the first attempt.
        for (int attempt = 0; attempt < 5; ++attempt) {
          if (OpenClipboard(nullptr)) {
            opened = true;
            return true;
          }

          Sleep(5);
        }

        return false;
      }

      ~clipboard_guard_t() {
        if (opened) {
          CloseClipboard();
        }
      }

      clipboard_guard_t(const clipboard_guard_t &) = delete;
      clipboard_guard_t &operator=(const clipboard_guard_t &) = delete;

    private:
      bool opened = false;
    };
  }  // namespace

  bool start(HWND window) {
    if (window == nullptr) {
      BOOST_LOG(error) << "Cannot register clipboard listener without a window";
      return false;
    }

    if (!AddClipboardFormatListener(window)) {
      BOOST_LOG(error)
        << "Failed to register Windows clipboard listener: "
        << GetLastError();
      return false;
    }

    BOOST_LOG(info) << "Windows text clipboard listener enabled";
    return true;
  }

  void stop(HWND window) {
    if (window == nullptr) {
      return;
    }

    if (!RemoveClipboardFormatListener(window)) {
      const DWORD error = GetLastError();

      // Do not treat shutdown cleanup as fatal.
      BOOST_LOG(warning)
        << "Failed to remove Windows clipboard listener: "
        << error;
      return;
    }

    BOOST_LOG(info) << "Windows text clipboard listener disabled";
  }

  std::optional<std::string> read_text() {
    std::scoped_lock lock(clipboard_mutex);

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
      return std::nullopt;
    }

    clipboard_guard_t clipboard;

    if (!clipboard.open()) {
      BOOST_LOG(warning) << "Unable to open Windows clipboard for reading";
      return std::nullopt;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);

    if (handle == nullptr) {
      return std::nullopt;
    }

    const SIZE_T allocation_size = GlobalSize(handle);

    if (allocation_size < sizeof(wchar_t)) {
      return std::nullopt;
    }

    const auto *data =
      static_cast<const wchar_t *>(GlobalLock(handle));

    if (data == nullptr) {
      return std::nullopt;
    }

    const std::size_t capacity =
      allocation_size / sizeof(wchar_t);

    const auto terminator =
      std::find(data, data + capacity, L'\0');

    const auto length =
      static_cast<std::size_t>(terminator - data);

    // Reject malformed handles without a null terminator.
    if (terminator == data + capacity) {
      GlobalUnlock(handle);
      BOOST_LOG(warning)
        << "Rejected unterminated Windows clipboard text";
      return std::nullopt;
    }

    std::wstring wide_text(data, length);
    GlobalUnlock(handle);

    std::string utf8_text;

    try {
      utf8_text = utf_utils::to_utf8(wide_text);
    }
    catch (...) {
      BOOST_LOG(warning)
        << "Rejected invalid Windows clipboard text";
      return std::nullopt;
    }

    if (utf8_text.size() > MAX_TEXT_BYTES) {
      BOOST_LOG(warning)
        << "Rejected oversized Windows clipboard text: "
        << utf8_text.size()
        << " bytes";
      return std::nullopt;
    }

    return utf8_text;
  }

  bool write_text(std::string_view utf8_text) {
    if (utf8_text.size() > MAX_TEXT_BYTES) {
      BOOST_LOG(warning)
        << "Refusing to write oversized clipboard text: "
        << utf8_text.size()
        << " bytes";
      return false;
    }

    std::wstring wide_text;

    try {
      wide_text = utf_utils::from_utf8(
        std::string(utf8_text)
      );
    }
    catch (...) {
      BOOST_LOG(warning)
        << "Refusing to write invalid UTF-8 clipboard text";
      return false;
    }

    std::scoped_lock lock(clipboard_mutex);
    clipboard_guard_t clipboard;

    if (!clipboard.open()) {
      BOOST_LOG(warning)
        << "Unable to open Windows clipboard for writing";
      return false;
    }

    const SIZE_T byte_count =
      (wide_text.size() + 1) * sizeof(wchar_t);

    HGLOBAL memory = GlobalAlloc(
      GMEM_MOVEABLE,
      byte_count
    );

    if (memory == nullptr) {
      BOOST_LOG(error)
        << "Failed to allocate Windows clipboard memory";
      return false;
    }

    void *target = GlobalLock(memory);

    if (target == nullptr) {
      GlobalFree(memory);
      return false;
    }

    std::memcpy(
      target,
      wide_text.c_str(),
      byte_count
    );

    GlobalUnlock(memory);

    if (!EmptyClipboard()) {
      GlobalFree(memory);
      return false;
    }

    // SetClipboardData takes ownership after success.
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
      GlobalFree(memory);
      return false;
    }

    ignore_next_update = true;
    return true;
  }

  void handle_update() {
    {
      std::scoped_lock lock(clipboard_mutex);

      if (ignore_next_update) {
        ignore_next_update = false;
        return;
      }
    }

    const auto text = read_text();

    if (!text.has_value()) {
      return;
    }

    // Never log clipboard contents, previews, or hashes.
    BOOST_LOG(debug)
      << "Windows text clipboard update detected: "
      << text->size()
      << " UTF-8 bytes";
  }
}  // namespace platf::clipboard
