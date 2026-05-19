#include "SmbNative.h"

#include <utility>

namespace smb::native_smb {
namespace {

void wipe(std::vector<std::uint8_t> &bytes) {
  auto *ptr = reinterpret_cast<volatile std::uint8_t *>(bytes.data());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    ptr[i] = 0;
  }
}

} // namespace

SecretBuffer::SecretBuffer(std::vector<std::uint8_t> bytes)
    : m_bytes(std::move(bytes)) {}

SecretBuffer::~SecretBuffer() { clear(); }

SecretBuffer::SecretBuffer(SecretBuffer &&other) noexcept
    : m_bytes(std::move(other.m_bytes)) {
  other.clear();
}

SecretBuffer &SecretBuffer::operator=(SecretBuffer &&other) noexcept {
  if (this != &other) {
    clear();
    m_bytes = std::move(other.m_bytes);
    other.clear();
  }
  return *this;
}

const std::vector<std::uint8_t> &SecretBuffer::bytes() const {
  return m_bytes;
}

bool SecretBuffer::empty() const { return m_bytes.empty(); }

void SecretBuffer::clear() {
  wipe(m_bytes);
  m_bytes.clear();
}

void CancellationToken::cancel() { m_cancelled.store(true); }

bool CancellationToken::isCancellationRequested() const {
  return m_cancelled.load();
}

const BuildPolicy &buildPolicy() {
  static const BuildPolicy policy;
  return policy;
}

std::string version() { return "clean-room-scaffold"; }

} // namespace smb::native_smb
