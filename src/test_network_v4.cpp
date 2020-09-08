#include <boost/asio.hpp>
#include <iostream>


int main(int argc, char **argv)
{

  boost::asio::ip::network_v4 network = boost::asio::ip::make_network_v4("0.0.0.0/0");
  std::cout << "network cidr: " << network << std::endl;
  std::cout << "canonical: " << network.canonical() << std::endl;
  std::cout << "netmask: " << network.netmask() << std::endl;
  std::cout << "prefix_length: " << network.prefix_length() << std::endl;
  const auto hosts = network.hosts();
  for (auto h : {"192.168.0.5", "192.168.1.5", "192.167.0.5"}) {
    const auto net = boost::asio::ip::make_address_v4(h);
    bool found = hosts.find(net) != hosts.end();
    std::cout << net << ": " << found << std::endl;
  }
  

  return 0;
}
