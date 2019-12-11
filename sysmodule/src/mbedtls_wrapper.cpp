#include <mbedtls/error.h>
#include <sys/socket.h>

#include "mbedtls_wrapper.h"

std::string get_mbedtls_error(const char* name, int err) {
  char buf[128];
  mbedtls_strerror(err, buf, sizeof(buf));
  return buf;
}

MBedTLSWrapper::MBedTLSWrapper(const std::string& hostname) {
  mbedtls_entropy_init(&_entropy);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
  mbedtls_x509_crt_init(&_cacert);
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_ssl_conf);

  int ret;
  if ((ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
                                   nullptr, 0)) != 0) {
    _error = get_mbedtls_error("mbedtls_ctr_drbg_seed", ret);
    return;
  }

  if ((ret = mbedtls_ssl_config_defaults(&_ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
    _error = get_mbedtls_error("mbedtls_ssl_config_defaults", ret);
    return;
  }

  mbedtls_ssl_conf_ca_chain(&_ssl_conf, &_cacert, nullptr);
  mbedtls_ssl_conf_rng(&_ssl_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
  mbedtls_ssl_conf_authmode(&_ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

  if ((ret = mbedtls_ssl_setup(&_ssl, &_ssl_conf)) != 0) {
    _error = get_mbedtls_error("mbedtls_ssl_setup", ret);
    return;
  }

  mbedtls_ssl_set_hostname(&_ssl, hostname.c_str());
}

MBedTLSWrapper::~MBedTLSWrapper() {
  if (_fd > 0) {
    printf("Closing connection to %d\n", _fd);
    close(_fd);
  }

  mbedtls_entropy_free(&_entropy);
  mbedtls_ctr_drbg_free(&_ctr_drbg);
  mbedtls_x509_crt_free(&_cacert);
  mbedtls_ssl_free(&_ssl);
  mbedtls_ssl_config_free(&_ssl_conf);
}

bool MBedTLSWrapper::usable() const { return _error.empty(); }

std::string MBedTLSWrapper::get_error() const { return _error; }

void MBedTLSWrapper::set_fd(int fd) {
  _fd = fd;
  mbedtls_ssl_set_bio(
      &_ssl, this,
      [](void* ctx, const unsigned char* buf, size_t len) {
        auto mbedtls_wrapper = static_cast<MBedTLSWrapper*>(ctx);
        int size = send(mbedtls_wrapper->_fd, buf, len, 0);
        if (size < 0) {
          printf("Send error %d", size);
          return -1;
        }
        return size;
      },
      [](void* ctx, unsigned char* buf, size_t len) {
        auto mbedtls_wrapper = static_cast<MBedTLSWrapper*>(ctx);
        int size = recv(mbedtls_wrapper->_fd, buf, len, 0);
        if (size < 0) {
          printf("Receive error %d", size);
          return -1;
        }
        return size;
      },
      nullptr);
}

bool MBedTLSWrapper::start_ssl() {
  int ret = mbedtls_ssl_handshake(&_ssl);
  if (ret < 0) {
    _error = get_mbedtls_error("mbedtls_ssl_handshake", ret);
    return false;
  }
  return true;
}

bool MBedTLSWrapper::write(const char* data, size_t data_size) {
  int ret = mbedtls_ssl_write(&_ssl, (const unsigned char*)data, data_size);
  if (ret < 0) {
    _error = get_mbedtls_error("mbedtls_ssl_write", ret);
    return false;
  }
  return true;
}

size_t MBedTLSWrapper::read(char* buf, size_t buf_size) {
  return mbedtls_ssl_read(&_ssl, (unsigned char*)buf, buf_size);
}
