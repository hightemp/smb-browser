#include "DirectTcpTransport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace smb::native_smb {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
using SockLen = int;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

class WsaRuntime {
public:
  WsaRuntime() {
    WSADATA data;
    m_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }

  ~WsaRuntime() {
    if (m_ok) {
      WSACleanup();
    }
  }

  bool ok() const { return m_ok; }

private:
  bool m_ok = false;
};

bool ensureSocketRuntime() {
  static WsaRuntime runtime;
  return runtime.ok();
}

int lastSocketError() { return WSAGetLastError(); }

bool isWouldBlock(int error) {
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

void closeSocket(NativeSocket socket) { closesocket(socket); }

int setNonBlocking(NativeSocket socket) {
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode);
}

#else
using NativeSocket = int;
using SockLen = socklen_t;
constexpr NativeSocket kInvalidSocket = -1;

bool ensureSocketRuntime() { return true; }
int lastSocketError() { return errno; }

bool isWouldBlock(int error) {
  return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
}

void closeSocket(NativeSocket socket) { ::close(socket); }

int setNonBlocking(NativeSocket socket) {
  const auto flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}
#endif

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

std::chrono::steady_clock::time_point deadlineFor(
    const OperationContext &context) {
  const auto timeout =
      context.timeout.count() <= 0 ? std::chrono::milliseconds{30000}
                                   : context.timeout;
  return std::chrono::steady_clock::now() + timeout;
}

DecodeResult<bool> cancelledResult() {
  return DecodeResult<bool>::failure(ErrorCode::Cancelled,
                                     "SMB Direct TCP operation was cancelled.");
}

DecodeResult<ByteVector> cancelledBytesResult() {
  return DecodeResult<ByteVector>::failure(
      ErrorCode::Cancelled, "SMB Direct TCP operation was cancelled.");
}

DecodeResult<bool> timeoutResult() {
  return DecodeResult<bool>::failure(ErrorCode::Timeout,
                                     "SMB Direct TCP operation timed out.");
}

DecodeResult<ByteVector> timeoutBytesResult() {
  return DecodeResult<ByteVector>::failure(
      ErrorCode::Timeout, "SMB Direct TCP operation timed out.");
}

std::string socketErrorMessage(const char *operation, int error) {
  std::ostringstream stream;
  stream << operation << " failed with socket error " << error << ".";
  return stream.str();
}

ErrorCode mapSocketError(int error) {
#ifdef _WIN32
  switch (error) {
  case WSAETIMEDOUT:
    return ErrorCode::Timeout;
  case WSAECONNREFUSED:
  case WSAECONNRESET:
  case WSAEHOSTUNREACH:
  case WSAENETUNREACH:
    return ErrorCode::ServerUnavailable;
  case WSAEAFNOSUPPORT:
  case WSAEPROTONOSUPPORT:
    return ErrorCode::ProtocolUnsupported;
  default:
    return ErrorCode::NetworkError;
  }
#else
  switch (error) {
  case ETIMEDOUT:
    return ErrorCode::Timeout;
  case ECONNREFUSED:
  case ECONNRESET:
  case EHOSTUNREACH:
  case ENETUNREACH:
    return ErrorCode::ServerUnavailable;
  case EAFNOSUPPORT:
  case EPROTONOSUPPORT:
    return ErrorCode::ProtocolUnsupported;
  default:
    return ErrorCode::NetworkError;
  }
#endif
}

DecodeResult<bool> socketFailure(const char *operation, int error) {
  return DecodeResult<bool>::failure(mapSocketError(error),
                                     socketErrorMessage(operation, error));
}

DecodeResult<ByteVector> socketBytesFailure(const char *operation, int error) {
  return DecodeResult<ByteVector>::failure(
      mapSocketError(error), socketErrorMessage(operation, error));
}

const char *commandName(Command command) {
  switch (command) {
  case Command::Negotiate:
    return "NEGOTIATE";
  case Command::SessionSetup:
    return "SESSION_SETUP";
  case Command::Logoff:
    return "LOGOFF";
  case Command::TreeConnect:
    return "TREE_CONNECT";
  case Command::TreeDisconnect:
    return "TREE_DISCONNECT";
  case Command::Create:
    return "CREATE";
  case Command::Close:
    return "CLOSE";
  case Command::Flush:
    return "FLUSH";
  case Command::Read:
    return "READ";
  case Command::Write:
    return "WRITE";
  case Command::Lock:
    return "LOCK";
  case Command::Ioctl:
    return "IOCTL";
  case Command::Cancel:
    return "CANCEL";
  case Command::Echo:
    return "ECHO";
  case Command::QueryDirectory:
    return "QUERY_DIRECTORY";
  case Command::ChangeNotify:
    return "CHANGE_NOTIFY";
  case Command::QueryInfo:
    return "QUERY_INFO";
  case Command::SetInfo:
    return "SET_INFO";
  case Command::OplockBreak:
    return "OPLOCK_BREAK";
  }
  return "UNKNOWN";
}

std::string requestContext(const ByteVector &requestFrame) {
  const auto payload = decodeDirectTcpPayload(requestFrame);
  if (!payload.ok) {
    return {};
  }
  const auto header = decodeSmb2SyncHeader(payload.value);
  if (!header.ok) {
    return {};
  }

  std::ostringstream stream;
  stream << " while waiting for " << commandName(header.value.command)
         << " response message_id=0x" << std::hex << std::uppercase
         << std::setfill('0') << std::setw(16) << header.value.messageId;
  return stream.str();
}

DecodeResult<ByteVector>
withRequestContext(const DecodeResult<ByteVector> &result,
                   const ByteVector &requestFrame) {
  if (result.ok) {
    return result;
  }
  auto message = result.error.message;
  const auto context = requestContext(requestFrame);
  if (!context.empty()) {
    message += context;
    message += ".";
  }
  return DecodeResult<ByteVector>::failure(result.error.code,
                                           std::move(message));
}

DecodeResult<ByteVector> bytesFailureWithRequestContext(
    const DecodeResult<bool> &result, const ByteVector &requestFrame) {
  auto message = result.error.message;
  const auto context = requestContext(requestFrame);
  if (!context.empty()) {
    message += context;
    message += ".";
  }
  return DecodeResult<ByteVector>::failure(result.error.code,
                                           std::move(message));
}

DecodeResult<bool> waitForSocket(NativeSocket socket, bool writable,
                                 const OperationContext &context,
                                 std::chrono::steady_clock::time_point deadline) {
  while (true) {
    if (isCancelled(context)) {
      return cancelledResult();
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return timeoutResult();
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    const auto waitSlice =
        std::min<std::chrono::milliseconds>(remaining,
                                            std::chrono::milliseconds{100});

    fd_set readSet;
    fd_set writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    if (writable) {
      FD_SET(socket, &writeSet);
    } else {
      FD_SET(socket, &readSet);
    }

    timeval timeout;
    timeout.tv_sec = static_cast<long>(waitSlice.count() / 1000);
    timeout.tv_usec = static_cast<long>((waitSlice.count() % 1000) * 1000);

    const int nfds =
#ifdef _WIN32
        0;
#else
        socket + 1;
#endif
    const auto selected =
        select(nfds, writable ? nullptr : &readSet,
               writable ? &writeSet : nullptr, nullptr, &timeout);
    if (selected > 0) {
      return DecodeResult<bool>::success(true);
    }
    if (selected < 0) {
      const auto error = lastSocketError();
#ifndef _WIN32
      if (error == EINTR) {
        continue;
      }
#endif
      return socketFailure("select", error);
    }
  }
}

DecodeResult<NativeSocket> connectSocket(const DirectTcpEndpoint &endpoint,
                                         const OperationContext &context) {
  if (!ensureSocketRuntime()) {
    return DecodeResult<NativeSocket>::failure(
        ErrorCode::NetworkError, "Socket runtime initialization failed.");
  }

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  const auto port = std::to_string(endpoint.port);
  addrinfo *rawResults = nullptr;
  const auto resolveStatus =
      getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &rawResults);
  if (resolveStatus != 0 || rawResults == nullptr) {
    return DecodeResult<NativeSocket>::failure(
        ErrorCode::DnsError, "SMB Direct TCP host resolution failed.");
  }

  struct AddrInfoDeleter {
    void operator()(addrinfo *value) const { freeaddrinfo(value); }
  };
  std::unique_ptr<addrinfo, AddrInfoDeleter> results(rawResults);

  const auto deadline = deadlineFor(context);
  ProtocolError lastError{ErrorCode::ServerUnavailable,
                          "SMB Direct TCP connect failed."};

  for (auto *entry = results.get(); entry != nullptr; entry = entry->ai_next) {
    if (isCancelled(context)) {
      return DecodeResult<NativeSocket>::failure(
          ErrorCode::Cancelled, "SMB Direct TCP connect was cancelled.");
    }

    const auto socketHandle =
        ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (socketHandle == kInvalidSocket) {
      const auto error = lastSocketError();
      lastError = {mapSocketError(error), socketErrorMessage("socket", error)};
      continue;
    }

    if (setNonBlocking(socketHandle) != 0) {
      const auto error = lastSocketError();
      closeSocket(socketHandle);
      lastError = {mapSocketError(error),
                   socketErrorMessage("set non-blocking", error)};
      continue;
    }

    const auto connectResult =
        ::connect(socketHandle, entry->ai_addr,
                  static_cast<SockLen>(entry->ai_addrlen));
    if (connectResult == 0) {
      return DecodeResult<NativeSocket>::success(socketHandle);
    }

    const auto connectError = lastSocketError();
    if (!isWouldBlock(connectError)) {
      closeSocket(socketHandle);
      lastError = {mapSocketError(connectError),
                   socketErrorMessage("connect", connectError)};
      continue;
    }

    const auto ready = waitForSocket(socketHandle, true, context, deadline);
    if (!ready.ok) {
      closeSocket(socketHandle);
      return DecodeResult<NativeSocket>::failure(ready.error.code,
                                                 ready.error.message);
    }

    int socketError = 0;
    SockLen socketErrorSize = sizeof(socketError);
    if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
#ifdef _WIN32
                   reinterpret_cast<char *>(&socketError),
#else
                   &socketError,
#endif
                   &socketErrorSize) != 0 ||
        socketError != 0) {
      const auto error = socketError != 0 ? socketError : lastSocketError();
      closeSocket(socketHandle);
      lastError = {mapSocketError(error), socketErrorMessage("connect", error)};
      continue;
    }

    return DecodeResult<NativeSocket>::success(socketHandle);
  }

  return DecodeResult<NativeSocket>::failure(lastError.code, lastError.message);
}

} // namespace

DirectTcpTransport::DirectTcpTransport(DirectTcpEndpoint endpoint)
    : m_endpoint(std::move(endpoint)) {}

DirectTcpTransport::~DirectTcpTransport() { close(); }

DecodeResult<ByteVector>
DirectTcpTransport::exchange(const ByteVector &requestFrame,
                             const OperationContext &context) {
  const auto connected = connectIfNeeded(context);
  if (!connected.ok) {
    return DecodeResult<ByteVector>::failure(connected.error.code,
                                             connected.error.message);
  }

  const auto sent = sendAll(requestFrame, context);
  if (!sent.ok) {
    close();
    return bytesFailureWithRequestContext(sent, requestFrame);
  }

  auto header = receiveExact(kDirectTcpHeaderSize, context);
  if (!header.ok) {
    close();
    return withRequestContext(header, requestFrame);
  }

  const auto payloadLength = decodeDirectTcpPayloadLength(header.value);
  if (!payloadLength.ok) {
    close();
    return DecodeResult<ByteVector>::failure(payloadLength.error.code,
                                             payloadLength.error.message);
  }

  auto payload = receiveExact(payloadLength.value, context);
  if (!payload.ok) {
    close();
    return withRequestContext(payload, requestFrame);
  }

  ByteVector frame;
  frame.reserve(kDirectTcpHeaderSize + payload.value.size());
  frame.insert(frame.end(), header.value.begin(), header.value.end());
  frame.insert(frame.end(), payload.value.begin(), payload.value.end());
  return DecodeResult<ByteVector>::success(std::move(frame));
}

void DirectTcpTransport::close() {
  if (m_socket != static_cast<intptr_t>(kInvalidSocket)) {
    closeSocket(static_cast<NativeSocket>(m_socket));
    m_socket = static_cast<intptr_t>(kInvalidSocket);
  }
}

bool DirectTcpTransport::isConnectedForTests() const {
  return m_socket != static_cast<intptr_t>(kInvalidSocket);
}

DecodeResult<bool>
DirectTcpTransport::connectIfNeeded(const OperationContext &context) {
  if (m_socket != static_cast<intptr_t>(kInvalidSocket)) {
    return DecodeResult<bool>::success(true);
  }

  const auto socketResult = connectSocket(m_endpoint, context);
  if (!socketResult.ok) {
    return DecodeResult<bool>::failure(socketResult.error.code,
                                       socketResult.error.message);
  }
  m_socket = static_cast<intptr_t>(socketResult.value);
  return DecodeResult<bool>::success(true);
}

DecodeResult<bool> DirectTcpTransport::sendAll(
    const ByteVector &bytes, const OperationContext &context) {
  const auto deadline = deadlineFor(context);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (isCancelled(context)) {
      return cancelledResult();
    }

    const auto ready =
        waitForSocket(static_cast<NativeSocket>(m_socket), true, context,
                      deadline);
    if (!ready.ok) {
      return ready;
    }

    const auto remaining = bytes.size() - offset;
    const auto chunk = std::min<std::size_t>(
        remaining, static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const auto sent =
        ::send(static_cast<NativeSocket>(m_socket),
#ifdef _WIN32
               reinterpret_cast<const char *>(bytes.data() + offset),
#else
               bytes.data() + offset,
#endif
               static_cast<int>(chunk), 0);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent == 0) {
      return DecodeResult<bool>::failure(
          ErrorCode::NetworkError, "SMB Direct TCP socket closed during send.");
    }

    const auto error = lastSocketError();
    if (isWouldBlock(error)) {
      continue;
    }
    return socketFailure("send", error);
  }

  return DecodeResult<bool>::success(true);
}

DecodeResult<ByteVector>
DirectTcpTransport::receiveExact(std::size_t size,
                                 const OperationContext &context) {
  const auto deadline = deadlineFor(context);
  ByteVector bytes(size, 0);
  std::size_t offset = 0;
  while (offset < size) {
    if (isCancelled(context)) {
      return cancelledBytesResult();
    }

    const auto ready =
        waitForSocket(static_cast<NativeSocket>(m_socket), false, context,
                      deadline);
    if (!ready.ok) {
      return DecodeResult<ByteVector>::failure(ready.error.code,
                                               ready.error.message);
    }

    const auto remaining = size - offset;
    const auto chunk = std::min<std::size_t>(
        remaining, static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const auto received =
        ::recv(static_cast<NativeSocket>(m_socket),
#ifdef _WIN32
               reinterpret_cast<char *>(bytes.data() + offset),
#else
               bytes.data() + offset,
#endif
               static_cast<int>(chunk), 0);
    if (received > 0) {
      offset += static_cast<std::size_t>(received);
      continue;
    }
    if (received == 0) {
      return DecodeResult<ByteVector>::failure(
          ErrorCode::NetworkError,
          "SMB Direct TCP socket closed during receive.");
    }

    const auto error = lastSocketError();
    if (isWouldBlock(error)) {
      continue;
    }
    return socketBytesFailure("receive", error);
  }
  return DecodeResult<ByteVector>::success(std::move(bytes));
}

} // namespace smb::native_smb
