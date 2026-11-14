#include <boost/system/error_code.hpp>
int main() {
  boost::system::error_code ok;
  auto denied = make_error_code(boost::system::errc::permission_denied);
  return ok || !denied || denied.message().empty();
}
