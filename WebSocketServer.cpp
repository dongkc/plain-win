#include <iostream>
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/SecureServerSocket.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/Net/NetException.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Format.h"
#include "Poco/Net/HTTPSStreamFactory.h"
#include "Poco/Net/HTTPStreamFactory.h"
#include "Poco/Base64Encoder.h"
#include "Poco/Base64Decoder.h"
#include "Poco/File.h"
#include "Poco/Path.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "glog/logging.h"
#include "scanner.h"


using namespace std;
using namespace Poco;
using namespace Poco::Net;
using namespace Poco::Util;

std::string encode(const std::string &source)
{
    std::istringstream in(source);
    std::ostringstream out;
    Poco::Base64Encoder b64out(out);

    // not insert new line to b64out
    b64out.rdbuf()->setLineLength(0);

    std::copy(std::istreambuf_iterator<char>(in),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(b64out));
    b64out.close();

    return out.str();
}

bool json_parse(const std::string& json, std::string& taskid)
{
  rapidjson::Document doc;
  if (doc.Parse<0>(json.c_str()).HasParseError()) {
    LOG(INFO) << "msg parse failed: " << json;
    return false;
  }

  if((!doc.HasMember("taskid")) || (!doc["taskid"].IsString())) {
    LOG(INFO) << "taskid not found: " << json;
    return false;
  }
  taskid = doc["taskid"].GetString();

  return true;
}

std::string json_serialize(const std::string& taskid, int error, const std::string& base64)
{
  rapidjson::Document document;
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  rapidjson::Value root(rapidjson::kObjectType);

  rapidjson::Value cmd(rapidjson::kStringType);
  cmd.SetString("scan", 4, allocator);
  root.AddMember("command", cmd, allocator);

  rapidjson::Value task(rapidjson::kStringType);
  task.SetString(taskid.c_str(), taskid.size(), allocator);
  root.AddMember("taskid", task, allocator);

  rapidjson::Value error_code(rapidjson::kNumberType);
  error_code.SetInt(error);
  root.AddMember("error", error_code, allocator);

  if (error == 0) {
    rapidjson::Value base64_(rapidjson::kStringType);
    base64_.SetString(base64.c_str(), base64.size(), allocator);
    root.AddMember("result", base64_, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  root.Accept(writer);
  return buffer.GetString();
}

class WebSocketRequestHandler: public HTTPRequestHandler
{
  public:
    void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
    {
      try
      {
        WebSocket ws(request, response);
        ws.setReceiveTimeout(Poco::Timespan(10, 0, 0, 0, 0));
        LOG(INFO) << "WebSocket connection established.";
        char buffer[1024];
        int flags;
        int n;
        do
        {
          n = ws.receiveFrame(buffer, sizeof(buffer), flags);
          LOG(INFO) << "Frame received lenght: " << n << " flags: " << flags;

          if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_PING) {
            ws.sendFrame(buffer, n, WebSocket::FRAME_OP_PONG);
            LOG(INFO) << "send out pong frame";
          }
          else {
            string json(buffer, n);
            LOG(INFO) << "content: " << json;

            string taskid;
            if (json_parse(json, taskid)) {
              string image;
              int error = scan_doc(image);
              string base64(encode(image));
              LOG(INFO) << "return code: " << error;
              string msg(json_serialize(taskid, error, base64));

              ws.sendFrame(msg.data(), msg.size(), flags);
            };
          }
        }
        while ((flags & WebSocket::FRAME_OP_BITMASK) != WebSocket::FRAME_OP_CLOSE);
        LOG(INFO) << "WebSocket connection closed.";
      }
      catch (WebSocketException& exc)
      {
        LOG(INFO) << "Exception: " << exc.what();
        switch (exc.code())
        {
          case WebSocket::WS_ERR_HANDSHAKE_UNSUPPORTED_VERSION:
            response.set("Sec-WebSocket-Version", WebSocket::WEBSOCKET_VERSION);
            // fallthrough
          case WebSocket::WS_ERR_NO_HANDSHAKE:
          case WebSocket::WS_ERR_HANDSHAKE_NO_VERSION:
          case WebSocket::WS_ERR_HANDSHAKE_NO_KEY:
            response.setStatusAndReason(HTTPResponse::HTTP_BAD_REQUEST);
            response.setContentLength(0);
            response.send();
            break;
        }
      }
    }
};


class RequestHandlerFactory: public HTTPRequestHandlerFactory
{
  public:
    HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request)
    {
      LOG(INFO) << "Request from "
                <<  request.clientAddress().toString()
                <<  ": "
                <<  request.getMethod()
                <<  " "
                <<  request.getURI()
                <<  " "
                <<  request.getVersion();

      for (HTTPServerRequest::ConstIterator it = request.begin(); it != request.end(); ++it)
      {
        LOG(INFO) << it->first << ": " << it->second;
      }

      return new WebSocketRequestHandler;
    }
};


class WebSocketServer: public Poco::Util::ServerApplication
{
  public:
    WebSocketServer(): _helpRequested(false)
  {
    Poco::Net::initializeSSL();
    Poco::Net::HTTPStreamFactory::registerFactory();
    Poco::Net::HTTPSStreamFactory::registerFactory();
  }

    ~WebSocketServer()
    {
      Poco::Net::uninitializeSSL();
    }

  protected:
    void initialize(Application& self)
    {
      loadConfiguration(); // load default configuration files, if present
      ServerApplication::initialize(self);
      logger().information("starting up");

      if (!config().getBool("application.runAsService", false)) {
        Path path(config().getString("application.dir"));
        path.pushDirectory("logs");
        File f(path);
        if (!f.exists()) {
          f.createDirectory();
        }

        string base_name = config().getString("application.baseName");
        path.setFileName(base_name + "_");

        google::InitGoogleLogging(base_name.c_str());
        google::SetLogDestination(google::GLOG_INFO, path.toString().c_str());
      }

      printProperties("");
    }

    void uninitialize()
    {
      ServerApplication::uninitialize();
    }

    void defineOptions(OptionSet& options)
    {
      ServerApplication::defineOptions(options);

      options.addOption(
          Option("help", "h", "display help information on command line arguments")
          .required(false)
          .repeatable(false));
    }

    void handleOption(const std::string& name, const std::string& value)
    {
      ServerApplication::handleOption(name, value);

      if (name == "help")
        _helpRequested = true;
    }

    void displayHelp()
    {
      HelpFormatter helpFormatter(options());
      helpFormatter.setCommand(commandName());
      helpFormatter.setUsage("OPTIONS");
      helpFormatter.setHeader("A sample HTTP server supporting the WebSocket protocol.");
      helpFormatter.format(std::cout);
    }

    int main(const std::vector<std::string>& args)
    {
      if (_helpRequested)
      {
        displayHelp();
      }
      else
      {
        // get parameters from configuration file
        unsigned short port = (unsigned short) config().getInt("WebSocketServer.port", 9980);

        SecureServerSocket svs(port);
        HTTPServer srv(new RequestHandlerFactory, svs, new HTTPServerParams);
        srv.start();

        waitForTerminationRequest();

        srv.stop();
      }
      return Application::EXIT_OK;
    }

    void printProperties(const std::string& base)
    {
      AbstractConfiguration::Keys keys;
      config().keys(base, keys);
      if (keys.empty()) {
        if (config().hasProperty(base)) {
          std::string msg;
          msg.append(base);
          msg.append(" = ");
          msg.append(config().getString(base));
          LOG(INFO) << msg;
        }
      } else {
        for (AbstractConfiguration::Keys::const_iterator it = keys.begin(); it != keys.end(); ++it) {
          std::string fullKey = base;
          if (!fullKey.empty()) fullKey += '.';
          fullKey.append(*it);
          printProperties(fullKey);
        }
      }
    }

  private:
    bool _helpRequested;
};


POCO_SERVER_MAIN(WebSocketServer)
