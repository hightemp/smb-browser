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

NativeMetadataCapabilities NativeSmbConnection::metadataCapabilities() const {
  return m_session.metadataCapabilities();
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::setBasicInformation(const std::string &path,
                                         const FileBasicInformation &info,
                                         const OperationContext &context) {
  return m_session.setBasicInformation(path, info, context);
}

DecodeResult<NativeExtendedAttributesResult>
NativeSmbConnection::listExtendedAttributes(const std::string &path,
                                            const OperationContext &context) {
  return m_session.listExtendedAttributes(path, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::setExtendedAttributes(
    const std::string &path,
    const std::vector<FileFullEaInformation> &entries,
    const OperationContext &context) {
  return m_session.setExtendedAttributes(path, entries, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::removeExtendedAttribute(const std::string &path,
                                             const std::string &name,
                                             const OperationContext &context) {
  return m_session.removeExtendedAttribute(path, name, context);
}

DecodeResult<NativeSecurityDescriptorResult>
NativeSmbConnection::querySecurityDescriptor(
    const std::string &path, std::uint32_t securityInformation,
    const OperationContext &context) {
  return m_session.querySecurityDescriptor(path, securityInformation, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::setSecurityDescriptor(const std::string &path,
                                           std::uint32_t securityInformation,
                                           const ByteVector &descriptor,
                                           const OperationContext &context) {
  return m_session.setSecurityDescriptor(path, securityInformation, descriptor,
                                         context);
}

DecodeResult<NativeReadResult>
NativeSmbConnection::readFileOnce(const std::string &path,
                                  std::uint32_t length,
                                  std::uint64_t offset,
                                  const OperationContext &context) {
  return m_session.readFileOnce(path, length, offset, context);
}

DecodeResult<NativeFileHandle>
NativeSmbConnection::openFileForRead(const std::string &path,
                                     const OperationContext &context) {
  return m_session.openFileForRead(path, context);
}

DecodeResult<NativeReadResult>
NativeSmbConnection::readFileChunk(const FileId &fileId, std::uint32_t length,
                                   std::uint64_t offset,
                                   const OperationContext &context) {
  return m_session.readFileChunk(fileId, length, offset, context);
}

DecodeResult<bool>
NativeSmbConnection::closeFileHandle(const FileId &fileId,
                                     const OperationContext &context) {
  return m_session.closeFileHandle(fileId, context);
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

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::createHardLink(const std::string &existingPath,
                                    const std::string &linkPath,
                                    bool replaceIfExists,
                                    const OperationContext &context) {
  return m_session.createHardLink(existingPath, linkPath, replaceIfExists,
                                  context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::createSymbolicLink(const std::string &linkPath,
                                        const std::string &targetPath,
                                        bool directory, bool relative,
                                        const OperationContext &context) {
  return m_session.createSymbolicLink(linkPath, targetPath, directory,
                                      relative, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbConnection::copyFileServerSide(const std::string &sourcePath,
                                        const std::string &targetPath,
                                        std::uint64_t size,
                                        const OperationContext &context) {
  return m_session.copyFileServerSide(sourcePath, targetPath, size, context);
}

DecodeResult<NativeNotifyResult> NativeSmbConnection::watchDirectoryOnce(
    const std::string &path, std::uint32_t completionFilter, bool watchTree,
    const OperationContext &context) {
  return m_session.watchDirectoryOnce(path, completionFilter, watchTree,
                                      context);
}

DecodeResult<NativeShareList>
NativeSmbConnection::listShares(const std::string &serverName,
                                const OperationContext &context) {
  return m_session.listShares(serverName, context);
}

DecodeResult<NativeDfsReferralResult>
NativeSmbConnection::getDfsReferrals(const std::string &requestPath,
                                     const OperationContext &context) {
  return m_session.getDfsReferrals(requestPath, context);
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
