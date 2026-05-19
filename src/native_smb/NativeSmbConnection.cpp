#include "NativeSmbConnection.h"

#include <chrono>
#include <utility>

namespace smb::native_smb {
namespace {

DecodeResult<ByteVector> exchangePayload(Transport &transport,
                                         const ByteVector &request,
                                         const OperationContext &context) {
  const auto frame = transport.exchange(encodeDirectTcpFrame(request), context);
  if (!frame.ok) {
    return DecodeResult<ByteVector>::failure(frame.error.code,
                                             frame.error.message);
  }
  return decodeDirectTcpPayload(frame.value);
}

DecodeResult<bool> boolFailureFrom(const ProtocolError &error) {
  return DecodeResult<bool>::failure(error.code, error.message);
}

} // namespace

NativeSmbConnection::NativeSmbConnection(
    std::unique_ptr<Transport> transport, NativeSmbSessionConfig sessionConfig)
    : m_transport(std::move(transport)), m_session(*m_transport, sessionConfig) {}

NativeSmbConnection::~NativeSmbConnection() {
  OperationContext context;
  context.timeout = std::chrono::milliseconds{1000};
  (void)disconnect(context);
}

NativeSmbSession &NativeSmbConnection::session() { return m_session; }

const NativeSmbSession &NativeSmbConnection::session() const {
  return m_session;
}

DecodeResult<NativeDirectoryListing>
NativeSmbConnection::listDirectory(const std::string &path,
                                   const OperationContext &context) {
  return m_session.listDirectory(path, context);
}

DecodeResult<NativeStatResult>
NativeSmbConnection::statObject(const std::string &path,
                                const OperationContext &context) {
  return m_session.statObject(path, context);
}

DecodeResult<NativeReadResult>
NativeSmbConnection::readFileOnce(const std::string &path,
                                  std::uint32_t length,
                                  std::uint64_t offset,
                                  const OperationContext &context) {
  return m_session.readFileOnce(path, length, offset, context);
}

DecodeResult<NativeWriteResult>
NativeSmbConnection::writeFileOnce(const std::string &path,
                                   const ByteVector &data,
                                   std::uint64_t offset,
                                   const OperationContext &context) {
  return m_session.writeFileOnce(path, data, offset, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::createDirectory(const std::string &path,
                                     const OperationContext &context) {
  return m_session.createDirectory(path, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::deleteObject(const std::string &path, bool directory,
                                  const OperationContext &context) {
  return m_session.deleteObject(path, directory, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::deleteTree(const std::string &path,
                                const OperationContext &context) {
  return m_session.deleteTree(path, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::deleteWildcard(const std::string &parentPath,
                                    const std::string &pattern,
                                    const OperationContext &context) {
  return m_session.deleteWildcard(parentPath, pattern, context);
}

DecodeResult<NativeObjectMutationResult> NativeSmbConnection::renameObject(
    const std::string &fromPath, const std::string &toPath,
    bool replaceIfExists, const OperationContext &context) {
  return m_session.renameObject(fromPath, toPath, replaceIfExists, context);
}

DecodeResult<bool>
NativeSmbConnection::disconnect(const OperationContext &context) {
  if (m_disconnected) {
    return DecodeResult<bool>::success(true);
  }
  if (!m_transport) {
    return DecodeResult<bool>::failure(
        ErrorCode::InternalError, "Native SMB connection has no transport.");
  }

  auto messageId = m_session.allocateMessageIds(1);
  const auto treeRequest = buildTreeDisconnectRequest(
      messageId, m_session.treeId(), m_session.sessionId());
  const auto treePayload = exchangePayload(*m_transport, treeRequest, context);
  if (!treePayload.ok) {
    return boolFailureFrom(treePayload.error);
  }
  const auto treeResponse = decodeTreeDisconnectResponse(treePayload.value);
  if (!treeResponse.ok) {
    return boolFailureFrom(treeResponse.error);
  }

  messageId = m_session.allocateMessageIds(1);
  const auto logoffRequest =
      buildLogoffRequest(messageId, m_session.sessionId());
  const auto logoffPayload =
      exchangePayload(*m_transport, logoffRequest, context);
  if (!logoffPayload.ok) {
    return boolFailureFrom(logoffPayload.error);
  }
  const auto logoffResponse = decodeLogoffResponse(logoffPayload.value);
  if (!logoffResponse.ok) {
    return boolFailureFrom(logoffResponse.error);
  }

  m_disconnected = true;
  return DecodeResult<bool>::success(true);
}

} // namespace smb::native_smb
