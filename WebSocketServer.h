#ifndef WEB_SOCKET_SERVER_H
#define WEB_SOCKET_SERVER_H
#include <string>
#include <vector>
#include "Poco/Util/Application.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/SecureServerSocket.h"

class WebSocketServer: public Poco::Util::Application
{
  public:
  WebSocketServer();

  ~WebSocketServer();

  protected:
    void initialize(Poco::Util::Application& self);

    void uninitialize();

    void defineOptions(Poco::Util::OptionSet& options);

    void handleOption(const std::string& name, const std::string& value);

    void displayHelp();

    int main(const std::vector<std::string>& args);

    void printProperties(const std::string& base);

  private:
    bool _helpRequested;
    Poco::Net::SecureServerSocket* svs;
    Poco::Net::HTTPServer* srv;
};

#endif
