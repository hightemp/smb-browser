#pragma once

#include "Transport.h"

#include <cstdint>
#include <string>

namespace smb::native_smb {

struct DirectTcpEndpoint {
  std::string host;
  std::uint16_t port = 445;
};

class DirectTcpTransport final : public Transport {
public:
  explicit DirectTcpTransport(DirectTcpEndpoint endpoint);
  ~DirectTcpTransport() override;

  DirectTcpTransport(const DirectTcpTransport &) = delete;
  DirectTcpTransport &operator=(const DirectTcpTransport &) = delete;

  DecodeResult<ByteVector>
  exchange(const ByteVector &requestFrame, const OperationContext &context) override;

  void close();
  bool isConnectedForTests() const;

private:
  DecodeResult<bool> connectIfNeeded(const OperationContext &context);
  DecodeResult<bool> sendAll(const ByteVector &bytes,
                             const OperationContext &context);
  DecodeResult<ByteVector> receiveExact(std::size_t size,
                                        const OperationContext &context);

  DirectTcpEndpoint m_endpoint;
  intptr_t m_socket = -1;
};

} // namespace smb::native_smb
