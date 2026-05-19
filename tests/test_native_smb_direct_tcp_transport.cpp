#include "DirectTcpTransport.h"

#include <QtTest/QtTest>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32

class LoopbackServer final {
public:
  explicit LoopbackServer(smb::native_smb::ByteVector responseFrame)
      : m_responseFrame(std::move(responseFrame)) {
    m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
      throw std::runtime_error("socket failed");
    }

    int reuse = 1;
    if (::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse,
                     sizeof(reuse)) != 0) {
      throw std::runtime_error("setsockopt failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(m_socket, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) != 0) {
      throw std::runtime_error("bind failed");
    }
    if (::listen(m_socket, 1) != 0) {
      throw std::runtime_error("listen failed");
    }

    socklen_t addressLength = sizeof(address);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr *>(&address),
                      &addressLength) != 0) {
      throw std::runtime_error("getsockname failed");
    }
    m_port = ntohs(address.sin_port);
    m_thread = std::thread([this] { run(); });
  }

  ~LoopbackServer() {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_socket >= 0) {
      ::close(m_socket);
    }
  }

  std::uint16_t port() const { return m_port; }
  const smb::native_smb::ByteVector &requestFrame() const {
    return m_requestFrame;
  }

private:
  static bool receiveExact(int socket, std::uint8_t *data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
      const auto received =
          ::recv(socket, data + offset, size - offset, 0);
      if (received <= 0) {
        return false;
      }
      offset += static_cast<std::size_t>(received);
    }
    return true;
  }

  void run() {
    const auto client = ::accept(m_socket, nullptr, nullptr);
    if (client < 0) {
      return;
    }

    smb::native_smb::ByteVector header(smb::native_smb::kDirectTcpHeaderSize);
    if (receiveExact(client, header.data(), header.size())) {
      const auto length = smb::native_smb::decodeDirectTcpPayloadLength(header);
      if (length.ok) {
        smb::native_smb::ByteVector payload(length.value);
        if (receiveExact(client, payload.data(), payload.size())) {
          m_requestFrame = header;
          m_requestFrame.insert(m_requestFrame.end(), payload.begin(),
                                payload.end());
        }
      }
    }

    const auto split = std::min<std::size_t>(2, m_responseFrame.size());
    if (split > 0) {
      ::send(client, m_responseFrame.data(), split, 0);
    }
    if (split < m_responseFrame.size()) {
      ::send(client, m_responseFrame.data() + split,
             m_responseFrame.size() - split, 0);
    }
    ::close(client);
  }

  int m_socket = -1;
  std::uint16_t m_port = 0;
  smb::native_smb::ByteVector m_responseFrame;
  smb::native_smb::ByteVector m_requestFrame;
  std::thread m_thread;
};

#endif

} // namespace

class NativeSmbDirectTcpTransportTest final : public QObject {
  Q_OBJECT

private slots:
  void exchangesDirectTcpFrameWithLoopbackServer() {
#ifdef _WIN32
    QSKIP("Loopback server test is POSIX-only for now.");
#else
    const auto requestFrame =
        smb::native_smb::encodeDirectTcpFrame({'r', 'e', 'q'});
    const auto responseFrame =
        smb::native_smb::encodeDirectTcpFrame({'o', 'k'});
    LoopbackServer server(responseFrame);

    smb::native_smb::DirectTcpTransport transport(
        {"127.0.0.1", server.port()});
    smb::native_smb::OperationContext context;
    context.timeout = std::chrono::milliseconds{3000};

    const auto result = transport.exchange(requestFrame, context);

    QVERIFY(result.ok);
    QVERIFY(result.value == responseFrame);
    QVERIFY(server.requestFrame() == requestFrame);
    QVERIFY(transport.isConnectedForTests());
#endif
  }

  void returnsCancelledBeforeOpeningSocket() {
    smb::native_smb::DirectTcpTransport transport({"127.0.0.1", 445});
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;
    context.timeout = std::chrono::milliseconds{100};

    const auto result =
        transport.exchange(smb::native_smb::encodeDirectTcpFrame({'x'}),
                           context);

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
    QVERIFY(!transport.isConnectedForTests());
  }

  void returnsCancelledByCallbackBeforeOpeningSocket() {
    smb::native_smb::DirectTcpTransport transport({"127.0.0.1", 445});
    smb::native_smb::OperationContext context;
    context.cancellationCallback = []() { return true; };
    context.timeout = std::chrono::milliseconds{100};

    const auto result =
        transport.exchange(smb::native_smb::encodeDirectTcpFrame({'x'}),
                           context);

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
    QVERIFY(!transport.isConnectedForTests());
  }
};

QTEST_MAIN(NativeSmbDirectTcpTransportTest)

#include "test_native_smb_direct_tcp_transport.moc"
