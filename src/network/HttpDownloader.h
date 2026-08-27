#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files. The default
 * STANDARD mode preserves legacy callers, including local plain-http servers.
 * Hard-coded update services opt into VERIFIED_TLS, which requires HTTPS and
 * validates the certificate and hostname against ESP-IDF's CA bundle.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  enum class TransportSecurity : uint8_t {
    STANDARD,
    // Require HTTPS certificate and hostname verification against ESP-IDF's
    // bundled CA roots. Intended for hard-coded update services. A TLS error
    // fails closed instead of falling back to an unverified connection.
    VERIFIED_TLS,
  };

  struct ResponseInfo {
    size_t contentLength = 0;
    std::string sha256;
    std::string calendarDate;
    std::string serverDate;
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "", TransportSecurity security = TransportSecurity::STANDARD);

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", TransportSecurity security = TransportSecurity::STANDARD);

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", TransportSecurity security = TransportSecurity::STANDARD);

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      ResponseInfo* responseInfo = nullptr, uint32_t timeoutMs = 60000,
                                      TransportSecurity security = TransportSecurity::STANDARD);
};
