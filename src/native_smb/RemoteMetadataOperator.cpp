#include "RemoteMetadataOperator.h"

#include "CloseExchanger.h"
#include "QueryInfoExchanger.h"
#include "SetInfoExchanger.h"

#include <utility>

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<RemoteMetadataMutationResult> cancelledMutation() {
  return DecodeResult<RemoteMetadataMutationResult>::failure(
      ErrorCode::Cancelled, "SMB metadata operation was cancelled.");
}

DecodeResult<RemoteExtendedAttributesResult> cancelledEaQuery() {
  return DecodeResult<RemoteExtendedAttributesResult>::failure(
      ErrorCode::Cancelled, "SMB EA query was cancelled.");
}

DecodeResult<RemoteSecurityDescriptorResult> cancelledSecurityQuery() {
  return DecodeResult<RemoteSecurityDescriptorResult>::failure(
      ErrorCode::Cancelled, "SMB security descriptor query was cancelled.");
}

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

CreateRequestOptions metadataOpenOptions(const std::string &path,
                                         std::uint32_t desiredAccess) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = desiredAccess;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = 0;
  return options;
}

DecodeResult<CreateResponse> openObject(Transport &transport,
                                        const CreateRequestOptions &options,
                                        std::uint64_t messageId,
                                        std::uint32_t treeId,
                                        std::uint64_t sessionId,
                                        const OperationContext &context) {
  const auto createRequest =
      buildCreateRequest(options, messageId, treeId, sessionId);
  const auto createPayload = exchangePayload(transport, createRequest, context);
  if (!createPayload.ok) {
    return DecodeResult<CreateResponse>::failure(createPayload.error.code,
                                                createPayload.error.message, 1);
  }
  const auto createResponse = decodeCreateResponse(createPayload.value);
  if (!createResponse.ok) {
    return DecodeResult<CreateResponse>::failure(createResponse.error.code,
                                                createResponse.error.message, 1);
  }
  return createResponse;
}

DecodeResult<RemoteMetadataMutationResult>
closeMutation(Transport &transport, const FileId &fileId,
              std::uint64_t messageId, std::uint32_t treeId,
              std::uint64_t sessionId, const OperationContext &context) {
  CloseRequestOptions closeOptions;
  closeOptions.fileId = fileId;
  const CloseExchanger closer;
  const auto closeResponse =
      closer.close(transport, closeOptions, messageId, treeId, sessionId,
                   context);
  if (!closeResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        closeResponse.error.code, closeResponse.error.message, 3);
  }

  RemoteMetadataMutationResult result;
  result.fileId = fileId;
  result.messagesUsed = 3;
  return DecodeResult<RemoteMetadataMutationResult>::success(result);
}

QueryInfoRequestOptions queryOptions(const FileId &fileId,
                                     std::uint8_t infoType,
                                     std::uint8_t fileInfoClass,
                                     std::uint32_t outputBufferLength,
                                     std::uint32_t additionalInformation = 0) {
  QueryInfoRequestOptions options;
  options.fileId = fileId;
  options.infoType = infoType;
  options.fileInfoClass = fileInfoClass;
  options.outputBufferLength = outputBufferLength;
  options.additionalInformation = additionalInformation;
  return options;
}

SetInfoRequestOptions setOptions(const FileId &fileId, std::uint8_t infoType,
                                 std::uint8_t fileInfoClass,
                                 ByteVector buffer,
                                 std::uint32_t additionalInformation = 0) {
  SetInfoRequestOptions options;
  options.fileId = fileId;
  options.infoType = infoType;
  options.fileInfoClass = fileInfoClass;
  options.buffer = std::move(buffer);
  options.additionalInformation = additionalInformation;
  return options;
}

std::uint32_t securityQueryAccess(std::uint32_t securityInformation) {
  auto access = kReadControlAccess | kFileReadAttributes;
  if ((securityInformation & kSaclSecurityInformation) != 0) {
    access |= kAccessSystemSecurity;
  }
  return access;
}

std::uint32_t securitySetAccess(std::uint32_t securityInformation) {
  auto access = kFileReadAttributes;
  if ((securityInformation & kOwnerSecurityInformation) != 0 ||
      (securityInformation & kGroupSecurityInformation) != 0) {
    access |= kWriteOwnerAccess;
  }
  if ((securityInformation & kDaclSecurityInformation) != 0) {
    access |= kWriteDacAccess;
  }
  if ((securityInformation & kSaclSecurityInformation) != 0) {
    access |= kAccessSystemSecurity;
  }
  return access;
}

} // namespace

DecodeResult<RemoteMetadataMutationResult>
RemoteMetadataOperator::setBasicInformation(
    Transport &transport, const std::string &path,
    const FileBasicInformation &info, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const auto createResponse = openObject(
      transport,
      metadataOpenOptions(path, kFileWriteAttributes | kFileReadAttributes),
      messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        createResponse.error.code, createResponse.error.message,
        createResponse.error.messagesUsed);
  }
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const SetInfoExchanger setInfo;
  const auto setResponse = setInfo.setInfo(
      transport,
      setOptions(createResponse.value.fileId, kInfoTypeFile,
                 kFileBasicInformation, buildFileBasicInformation(info)),
      messageId + 1, treeId, sessionId, context);
  if (!setResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        setResponse.error.code, setResponse.error.message, 2);
  }

  return closeMutation(transport, createResponse.value.fileId, messageId + 2,
                       treeId, sessionId, context);
}

DecodeResult<RemoteExtendedAttributesResult>
RemoteMetadataOperator::listExtendedAttributes(
    Transport &transport, const std::string &path, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledEaQuery();
  }

  const auto createResponse =
      openObject(transport,
                 metadataOpenOptions(path, kFileReadEa | kFileReadAttributes),
                 messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return DecodeResult<RemoteExtendedAttributesResult>::failure(
        createResponse.error.code, createResponse.error.message,
        createResponse.error.messagesUsed);
  }
  if (isCancelled(context)) {
    return cancelledEaQuery();
  }

  const QueryInfoExchanger queryInfo;
  const auto queryResponse = queryInfo.queryInfo(
      transport,
      queryOptions(createResponse.value.fileId, kInfoTypeFile,
                   kFileFullEaInformation, 65536),
      messageId + 1, treeId, sessionId, context);
  if (!queryResponse.ok) {
    return DecodeResult<RemoteExtendedAttributesResult>::failure(
        queryResponse.error.code, queryResponse.error.message, 2);
  }

  const auto entries = decodeFileFullEaInformation(queryResponse.value.buffer);
  if (!entries.ok) {
    return DecodeResult<RemoteExtendedAttributesResult>::failure(
        entries.error.code, entries.error.message, 2);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse = closer.close(transport, closeOptions,
                                         messageId + 2, treeId, sessionId,
                                         context);
  if (!closeResponse.ok) {
    return DecodeResult<RemoteExtendedAttributesResult>::failure(
        closeResponse.error.code, closeResponse.error.message, 3);
  }

  RemoteExtendedAttributesResult result;
  result.fileId = createResponse.value.fileId;
  result.entries = entries.value;
  result.messagesUsed = 3;
  return DecodeResult<RemoteExtendedAttributesResult>::success(
      std::move(result));
}

DecodeResult<RemoteMetadataMutationResult>
RemoteMetadataOperator::setExtendedAttributes(
    Transport &transport, const std::string &path,
    const std::vector<FileFullEaInformation> &entries,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const auto createResponse =
      openObject(transport,
                 metadataOpenOptions(path, kFileWriteEa | kFileReadAttributes),
                 messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        createResponse.error.code, createResponse.error.message,
        createResponse.error.messagesUsed);
  }
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const SetInfoExchanger setInfo;
  const auto setResponse = setInfo.setInfo(
      transport,
      setOptions(createResponse.value.fileId, kInfoTypeFile,
                 kFileFullEaInformation, buildFileFullEaInformation(entries)),
      messageId + 1, treeId, sessionId, context);
  if (!setResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        setResponse.error.code, setResponse.error.message, 2);
  }

  return closeMutation(transport, createResponse.value.fileId, messageId + 2,
                       treeId, sessionId, context);
}

DecodeResult<RemoteMetadataMutationResult>
RemoteMetadataOperator::removeExtendedAttribute(
    Transport &transport, const std::string &path, const std::string &name,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  FileFullEaInformation entry;
  entry.name = name;
  return setExtendedAttributes(transport, path, {entry}, messageId, treeId,
                               sessionId, context);
}

DecodeResult<RemoteSecurityDescriptorResult>
RemoteMetadataOperator::querySecurityDescriptor(
    Transport &transport, const std::string &path,
    std::uint32_t securityInformation, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledSecurityQuery();
  }

  const auto createResponse =
      openObject(transport,
                 metadataOpenOptions(path,
                                     securityQueryAccess(securityInformation)),
                 messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return DecodeResult<RemoteSecurityDescriptorResult>::failure(
        createResponse.error.code, createResponse.error.message,
        createResponse.error.messagesUsed);
  }
  if (isCancelled(context)) {
    return cancelledSecurityQuery();
  }

  const QueryInfoExchanger queryInfo;
  const auto queryResponse = queryInfo.queryInfo(
      transport,
      queryOptions(createResponse.value.fileId, kInfoTypeSecurity, 0, 65536,
                   securityInformation),
      messageId + 1, treeId, sessionId, context);
  if (!queryResponse.ok) {
    return DecodeResult<RemoteSecurityDescriptorResult>::failure(
        queryResponse.error.code, queryResponse.error.message, 2);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse = closer.close(transport, closeOptions,
                                         messageId + 2, treeId, sessionId,
                                         context);
  if (!closeResponse.ok) {
    return DecodeResult<RemoteSecurityDescriptorResult>::failure(
        closeResponse.error.code, closeResponse.error.message, 3);
  }

  RemoteSecurityDescriptorResult result;
  result.fileId = createResponse.value.fileId;
  result.descriptor = queryResponse.value.buffer;
  result.messagesUsed = 3;
  return DecodeResult<RemoteSecurityDescriptorResult>::success(
      std::move(result));
}

DecodeResult<RemoteMetadataMutationResult>
RemoteMetadataOperator::setSecurityDescriptor(
    Transport &transport, const std::string &path,
    std::uint32_t securityInformation, const ByteVector &descriptor,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const auto createResponse =
      openObject(transport,
                 metadataOpenOptions(path, securitySetAccess(securityInformation)),
                 messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        createResponse.error.code, createResponse.error.message,
        createResponse.error.messagesUsed);
  }
  if (isCancelled(context)) {
    return cancelledMutation();
  }

  const SetInfoExchanger setInfo;
  const auto setResponse = setInfo.setInfo(
      transport,
      setOptions(createResponse.value.fileId, kInfoTypeSecurity, 0, descriptor,
                 securityInformation),
      messageId + 1, treeId, sessionId, context);
  if (!setResponse.ok) {
    return DecodeResult<RemoteMetadataMutationResult>::failure(
        setResponse.error.code, setResponse.error.message, 2);
  }

  return closeMutation(transport, createResponse.value.fileId, messageId + 2,
                       treeId, sessionId, context);
}

} // namespace smb::native_smb
