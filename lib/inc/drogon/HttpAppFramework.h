/**
 *
 *  HttpAppFramework.h
 *  An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */

#pragma once

#include <drogon/utils/HttpConstraint.h>
#include <drogon/CacheMap.h>
#include <drogon/DrObject.h>
#include <drogon/HttpBinder.h>
#include <drogon/IntranetIpFilter.h>
#include <drogon/LocalHostFilter.h>
#include <drogon/MultiPart.h>
#include <drogon/NotFound.h>
#include <drogon/drogon_callbacks.h>
#include <drogon/utils/ClassTraits.h>
#include <drogon/utils/Utilities.h>
#include <drogon/plugins/Plugin.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <trantor/net/Resolver.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/NonCopyable.h>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <chrono>

namespace drogon
{
// the drogon banner
const char banner[] =
    "     _                             \n"
    "  __| |_ __ ___   __ _  ___  _ __  \n"
    " / _` | '__/ _ \\ / _` |/ _ \\| '_ \\ \n"
    "| (_| | | | (_) | (_| | (_) | | | |\n"
    " \\__,_|_|  \\___/ \\__, |\\___/|_| |_|\n"
    "                 |___/             \n";

std::string getVersion();
std::string getGitCommit();

class HttpControllerBase;
class HttpSimpleControllerBase;
class WebSocketControllerBase;

class HttpAppFramework : public trantor::NonCopyable
{
  public:
    virtual ~HttpAppFramework();
    /// Get the instance of HttpAppFramework
    /**
     * HttpAppFramework works at singleton mode, so any calling of this
     * method gets the same instance;
     * Calling drogon::HttpAppFramework::instance()
     * can be replaced by a simple interface -- drogon::app()
     */
    static HttpAppFramework &instance();

    /// Run the event loop
    /**
     * Calling this method starts the IO event loops and the main loop of the
     * application;
     * This method MUST be called in the main thread.
     * This method blocks the main thread until the main event loop exits.
     */
    virtual void run() = 0;

    /// Return true if the framework is running
    virtual bool isRunning() = 0;

    /// Quit the event loop
    /**
     * Calling this method results in stopping all network IO in the
     * framework and interrupting the blocking of the run() method. Usually,
     * after calling this method, the application exits.
     *
     * NOTE:
     * This method can be called in any thread and anywhere.
     * This method should not be called before calling run().
     */
    virtual void quit() = 0;

    /// Get the main event loop of the framework;
    /**
     * NOTE:
     * The event loop is not the network IO loop, but the main event loop
     * of the framework in which only some timer tasks are running;
     * User can run some timer tasks or other tasks in this loop;
     * This method can be call in any thread.
     */
    virtual trantor::EventLoop *getLoop() const = 0;

    /// Set custom 404 page
    /**
     * After calling this method, the @param resp object is returned
     * by the HttpResponse::newNotFoundResponse() method.
     */
    virtual HttpAppFramework &setCustom404Page(const HttpResponsePtr &resp) = 0;

    /// Get the plugin object registered in the framework
    /**
     * NOTE:
     * This method is usually called after the framework runs.
     * Calling this method in the initAndStart() method of plugins is also
     * valid.
     */
    template <typename T>
    T *getPlugin()
    {
        static_assert(IsPlugin<T>::value,
                      "The Template parameter must be a subclass of "
                      "PluginBase");
        assert(isRunning());
        return dynamic_cast<T *>(getPlugin(T::classTypeName()));
    }

    /// Get the plugin object registered in the framework
    /**
     * @param name: is the class name of the plugin.
     *
     * NOTE:
     * This method is usually called after the framework runs.
     * Calling this method in the initAndStart() method of plugins is also
     * valid.
     */
    virtual PluginBase *getPlugin(const std::string &name) = 0;

    /// The following is a series of methods of AOP

    /// The @param advice is called immediately after the main event loop runs.
    virtual HttpAppFramework &registerBeginningAdvice(
        const std::function<void()> &advice) = 0;

    /// The @param advice is called immediately when a new connection is
    /// established.
    /**
     * The first parameter of the @param advice is the remote address of the new
     * connection, the second one is the local address of it.
     * If the @param advice returns a false value, drogon closes the connection.
     * Users can use this advice to implement some security policies.
     */
    virtual HttpAppFramework &registerNewConnectionAdvice(
        const std::function<bool(const trantor::InetAddress &,
                                 const trantor::InetAddress &)> &advice) = 0;

    /// The @param advice is called immediately after the request is created. If
    /// a no-empty response is returned by the advice, it is sent to the client
    /// and no handler is invoked.
    /**
     * The following diagram shows the location of the AOP join points during
     * http request processing.
     *
     *
     *                   +-----------+                             +----------+
     *                   |  Request  |                             | Response |
     *                   +-----------+                             +----------+
     *                         |                                         ^
     *                         v                                         |
     *         sync join point o----------->[HttpResponsePtr]----------->+
     *                         |                                         |
     *                         v                                         |
     *  Pre-routing join point o----------->[Advice callback]----------->+
     *                         |                                         |
     *                         v         Invalid path                    |
     *                   [Find Handler]---------------->[404]----------->+
     *                         |                                         |
     *                         v                                         |
     * Post-routing join point o----------->[Advice callback]----------->+
     *                         |                                         |
     *                         v        Invalid method                   |
     *                   [Check Method]---------------->[405]----------->+
     *                         |                                         |
     *                         v                                         |
     *                     [Filters]------->[Filter callback]----------->+
     *                         |                                         |
     *                         v             Y                           |
     *                [Is OPTIONS method?]------------->[200]----------->+
     *                         |                                         |
     *                         v                                         |
     * Pre-handling join point o----------->[Advice callback]----------->+
     *                         |                                         |
     *                         v                                         |
     *                     [Handler]                                     |
     *                         |                                         |
     *                         v                                         |
     *Post-handling join point o---------------------------------------->+
     *
     */
    virtual HttpAppFramework &registerSyncAdvice(
        const std::function<HttpResponsePtr(const HttpRequestPtr &)>
            &advice) = 0;

    /// The @param advice is called after all the synchronous advices return
    /// nullptr and before the request is routed to any handler.
    /**
     * The parameters of the @param advice are same as those of the doFilter
     * method of the Filter class.
     */
    virtual HttpAppFramework &registerPreRoutingAdvice(
        const std::function<void(const HttpRequestPtr &,
                                 AdviceCallback &&,
                                 AdviceChainCallback &&)> &advice) = 0;

    /// The @param advice is called at the same time as the above advice. It can
    /// be thought of as an observer who cannot respond
    /// to http requests.
    /**
     * This advice has less overhead than the above one.
     * If one does not intend to intercept the http request, please use this
     * interface.
     */
    virtual HttpAppFramework &registerPreRoutingAdvice(
        const std::function<void(const HttpRequestPtr &)> &advice) = 0;

    /// The @param advice is called immediately after the request matchs a
    /// handler path and before any 'doFilter' method of filters applies.
    /**
     * The parameters of the @param advice are same as those of the doFilter
     * method of the Filter class.
     */
    virtual HttpAppFramework &registerPostRoutingAdvice(
        const std::function<void(const HttpRequestPtr &,
                                 AdviceCallback &&,
                                 AdviceChainCallback &&)> &advice) = 0;

    /// The @param advice is called at the same time as the above advice. It can
    /// be thought of as an observer who cannot respond
    /// to http requests.
    /**
     * This advice has less overhead than the above one.
     * If one does not intend to intercept the http request, please use this
     * interface.
     */
    virtual HttpAppFramework &registerPostRoutingAdvice(
        const std::function<void(const HttpRequestPtr &)> &advice) = 0;

    /// The @param advice is called immediately after the request is approved by
    /// all filters and before it is handled.
    /**
     * The parameters of the @param advice are same as those of the doFilter
     * method of the Filter class.
     */
    virtual HttpAppFramework &registerPreHandlingAdvice(
        const std::function<void(const HttpRequestPtr &,
                                 AdviceCallback &&,
                                 AdviceChainCallback &&)> &advice) = 0;

    /// The @param advice is called at the same time as the above advice. It can
    /// be thought of as an observer who cannot respond to http requests.
    /**
     * This advice has less overhead than the above one.
     * If one does not intend to intercept the http request, please use this
     * interface.
     */
    virtual HttpAppFramework &registerPreHandlingAdvice(
        const std::function<void(const HttpRequestPtr &)> &advice) = 0;

    /// The @param advice is called immediately after the request is handled and
    /// a response object is created by handlers.
    virtual HttpAppFramework &registerPostHandlingAdvice(
        const std::function<void(const HttpRequestPtr &,
                                 const HttpResponsePtr &)> &advice) = 0;

    /// End of AOP methods

    /// Load the configuration file with json format.
    virtual HttpAppFramework &loadConfigFile(const std::string &fileName) = 0;

    /// Register a HttpSimpleController object into the framework.
    /**
     * @param pathName: When the path of a http request is equal to the @param
     * pathName, the asyncHandleHttpRequest() method of the controller is
     * called.
     * @param ctrlName is the name of the controller. It includes the namespace
     * to which the controller belongs.
     * @param filtersAndMethods is a vector containing Http methods or filter
     * name constraints.
     *
     * FOR EXAMPLE:
     * app.registerHttpSimpleController("/userinfo","UserInfoCtrl",{Get,"LoginFilter"});
     *
     * NOTE:
     * Users can perform the same operation through the configuration file or a
     * macro in the header file.
     */
    virtual HttpAppFramework &registerHttpSimpleController(
        const std::string &pathName,
        const std::string &ctrlName,
        const std::vector<internal::HttpConstraint> &filtersAndMethods =
            std::vector<internal::HttpConstraint>{}) = 0;

    /// Register a handler into the framework.
    /**
     * @param pathPattern: When the path of a http request matches the @param
     * pathPattern, the handler indicated by
     * the @param function is called.
     * @param function indicates any type of callable object with a valid
     * processing interface.
     * @param filtersAndMethods is the same as the third parameter in the above
     * method.
     *
     * FOR EXAMPLE:
     *  app.registerHandler("/hello?username={1}",
     *                      [](const HttpRequestPtr& req,
     *                         std::function<void (const HttpResponsePtr
     * &)> &&callback,
     *                         const std::string &name)
     *                         {
     *                             Json::Value json;
     *                             json["result"]="ok";
     *                             json["message"]=std::string("hello,")+name;
     *                             auto
     * resp=HttpResponse::newHttpJsonResponse(json);
     *                             callback(resp);
     *                         },
     *                      {Get,"LoginFilter"});
     *
     * NOTE:
     * As you can see in the above example, this method supports parameters
     * mapping.
     */
    template <typename FUNCTION>
    HttpAppFramework &registerHandler(
        const std::string &pathPattern,
        FUNCTION &&function,
        const std::vector<internal::HttpConstraint> &filtersAndMethods =
            std::vector<internal::HttpConstraint>{},
        const std::string &handlerName = "")
    {
        LOG_TRACE << "pathPattern:" << pathPattern;
        internal::HttpBinderBasePtr binder;

        binder = std::make_shared<internal::HttpBinder<FUNCTION>>(
            std::forward<FUNCTION>(function));

        std::vector<HttpMethod> validMethods;
        std::vector<std::string> filters;
        for (auto const &filterOrMethod : filtersAndMethods)
        {
            if (filterOrMethod.type() == internal::ConstraintType::HttpFilter)
            {
                filters.push_back(filterOrMethod.getFilterName());
            }
            else if (filterOrMethod.type() ==
                     internal::ConstraintType::HttpMethod)
            {
                validMethods.push_back(filterOrMethod.getHttpMethod());
            }
            else
            {
                LOG_ERROR << "Invalid controller constraint type";
                exit(1);
            }
        }
        registerHttpController(
            pathPattern, binder, validMethods, filters, handlerName);
        return *this;
    }

    /// Register a WebSocketController into the framework.
    /// The parameters of this method are the same as those in the
    /// registerHttpSimpleController() method.
    virtual HttpAppFramework &registerWebSocketController(
        const std::string &pathName,
        const std::string &crtlName,
        const std::vector<std::string> &filters =
            std::vector<std::string>()) = 0;

    /// Register controller objects created and initialized by the user
    /**
     * Drogon can only automatically create controllers using the default
     * constructor.
     * Sometimes users want to be able to create controllers using constructors
     * with parameters. Controllers created by user in this way should be
     * registered to the framework via this method. The macro or configuration
     * file is still valid for the path routing configuration of the controller
     * created by users.
     *
     * NOTE:
     * The declaration of the controller class must be as follows:
     *   class ApiTest : public drogon::HttpController<ApiTest, false>
     *   {
     *       public:
     *           ApiTest(const std::string &str);
     *       ...
     *   };
     * The second template parameter must be explicitly set to false to disable
     * automatic creation.
     * And then user can create and register it somewhere as follows:
     *   auto ctrlPtr=std::make_shared<ApiTest>("hello world");
     *   drogon::app().registerController(ctrlPtr);
     * This method should be called before calling the app().run() method.
     */
    template <typename T>
    HttpAppFramework &registerController(const std::shared_ptr<T> &ctrlPtr)
    {
        static_assert(
            internal::IsSubClass<T, HttpControllerBase>::value ||
                internal::IsSubClass<T, HttpSimpleControllerBase>::value ||
                internal::IsSubClass<T, WebSocketControllerBase>::value,
            "Error! Only controller objects can be registered here");
        static_assert(!T::isAutoCreation,
                      "Controllers created and initialized "
                      "automatically by drogon cannot be "
                      "registered here");
        DrClassMap::setSingleInstance(ctrlPtr);
        T::initPathRouting();
        return *this;
    }

    /// Register filter objects created and initialized by the user
    /**
     * This method is similar to the above method.
     */
    template <typename T>
    HttpAppFramework &registerFilter(const std::shared_ptr<T> &filterPtr)
    {
        static_assert(internal::IsSubClass<T, HttpFilterBase>::value,
                      "Error! Only fitler objects can be registered here");
        static_assert(!T::isAutoCreation,
                      "Filters created and initialized "
                      "automatically by drogon cannot be "
                      "registered here");
        DrClassMap::setSingleInstance(filterPtr);
        return *this;
    }

    /// Forward the http request
    /**
     * The @param hostString is the address where the request is forwarded. The
     * following strings are valid for the @param hostString:
     *
     * https://www.baidu.com
     * http://www.baidu.com
     * https://127.0.0.1:8080/
     * http://127.0.0.1
     * http://[::1]:8080/
     *
     * NOTE:
     * If the @param hostString is empty, the request is handled by the same
     * application, so in this condition
     * one should modify the path of the @param req before forwarding to avoid
     * infinite loop processing.
     *
     * This method can be used to implement reverse proxy or redirection on the
     * server side.
     */
    virtual void forward(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string &hostString = "") = 0;

    /// Get information about the handlers registered to drogon
    /**
     * The first item of std::tuple in the return value represents the path
     * pattern of the handler;
     * The last item in std::tuple is the description of the handler.
     */
    virtual std::vector<std::tuple<std::string, HttpMethod, std::string>>
    getHandlersInfo() const = 0;

    /// Get the custom configuration defined by users in the configuration file.
    virtual const Json::Value &getCustomConfig() const = 0;

    /// Set the number of threads for IO event loops
    /**
     * The default value is 1, if @param threadNum is 0, the number is equal to
     * the number of CPU cores.
     *
     * NOTE:
     * This number is usually less than or equal to the number of CPU cores.
     * This number can be configured in the configuration file.
     */
    virtual HttpAppFramework &setThreadNum(size_t threadNum) = 0;

    /// Get the number of threads for IO event loops
    virtual size_t getThreadNum() const = 0;

    /// Set the global cert file and private key file for https
    /// These options can be configured in the configuration file.
    virtual HttpAppFramework &setSSLFiles(const std::string &certPath,
                                          const std::string &keyPath) = 0;

    /// Add a listener for http or https service
    /**
     * @param ip is the ip that the listener listens on.
     * @param port is the port that the listener listens on.
     * If @param useSSL is true, the listener is used for the https service.
     * @param certFile and @param keyFile specify the cert file and the private
     * key file for this listener. If they are empty, the global configuration
     * set by the above method is used.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &addListener(const std::string &ip,
                                          uint16_t port,
                                          bool useSSL = false,
                                          const std::string &certFile = "",
                                          const std::string &keyFile = "") = 0;

    /// Enable sessions supporting.
    /**
     * Disabled by default.
     * If there isn't any request from a client for @param timeout(>0) seconds,
     * the session of the client is destroyed.
     * If the @param timeout is equal to 0, sessions will remain permanently
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &enableSession(const size_t timeout = 0) = 0;

    /// A wrapper of the above method.
    /**
     * Users can set the timeout value as follows:
     *   app().enableSession(0.2h);
     *   app().enableSession(12min);
     */
    inline HttpAppFramework &enableSession(
        const std::chrono::duration<long double> &timeout)
    {
        return enableSession((size_t)timeout.count());
    }

    /// Disable sessions supporting.
    /**
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &disableSession() = 0;

    /// Set the root path of HTTP document, defaut path is ./
    /**
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setDocumentRoot(const std::string &rootPath) = 0;

    /// Get the document root directory.
    virtual const std::string &getDocumentRoot() const = 0;

    /// Set the path to store uploaded files.
    /**
     * If the @param uploadPath isn't prefixed with /, ./ or ../, it is relative
     * path of document_root path,
     * The default value is 'uploads'.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setUploadPath(const std::string &uploadPath) = 0;

    /// Get the path to store uploaded files.
    virtual const std::string &getUploadPath() const = 0;

    /// Set types of files that can be downloaded.
    /**
     * FOR EXAMPLE:
     * app.setFileTypes({"html","txt","png","jpg"});
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setFileTypes(
        const std::vector<std::string> &types) = 0;

    /// Enable supporting for dynamic views loading.
    /**
     * Disabled by default.
     * The @param libPaths is a vactor that contains paths to view files.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &enableDynamicViewsLoading(
        const std::vector<std::string> &libPaths) = 0;

    /// Set the maximum number of all connections.
    /**
     * The default value is 100000.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setMaxConnectionNum(size_t maxConnections) = 0;

    /// Set the maximum number of connections per remote IP.
    /**
     * The default value is 0 which means no limit.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setMaxConnectionNumPerIP(
        size_t maxConnectionsPerIP) = 0;

    /// Make the application run as a daemon.
    /**
     * Disabled by default.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &enableRunAsDaemon() = 0;

    /// Make the application restart after crashing.
    /**
     * Disabled by default.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &enableRelaunchOnError() = 0;

    /// Set the output path of logs.
    /**
     * @param logSize indicates the maximum size of the log file.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setLogPath(
        const std::string &logPath,
        const std::string &logfileBaseName = "",
        size_t logSize = 100000000) = 0;

    /// Set the log level
    /**
     * The @param level is one of TRACE, DEBUG, INFO, WARN. The Default value is
     * DEBUG.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setLogLevel(trantor::Logger::LogLevel level) = 0;

    /// If @param sendFile is true, sendfile() system-call is used to send
    /// static files to clients;
    /**
     * The default value is true.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     * Even though sendfile() is enabled, only files larger than 200k are sent
     * this way,
     * because the advantages of sendfile() can only be reflected in sending
     * large files.
     */
    virtual HttpAppFramework &enableSendfile(bool sendFile) = 0;

    /// If @param useGzip is true, use gzip to compress the response body's
    /// content;
    /**
     * The default value is true.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     * After gzip is enabled, gzip is used under the following conditions:
     * 1. The content type of response is not a binary type.
     * 2. The content length is bigger than 1024 bytes.
     */
    virtual HttpAppFramework &enableGzip(bool useGzip) = 0;

    /// Return true if gzip is enabled.
    virtual bool isGzipEnabled() const = 0;

    /// Set the time in which the static file response is cached in memory.
    /**
     * @param cacheTime: in seconds. 0 means always cached, negative means no
     * cache
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setStaticFilesCacheTime(int cacheTime) = 0;

    /// Get the time set by the above method.
    virtual int staticFilesCacheTime() const = 0;

    /// Set the lifetime of the connection without read or write
    /**
     * @param timeout: in seconds. 60 by default. Setting the timeout to 0 means
     * that drogon does not close idle connections.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setIdleConnectionTimeout(size_t timeout) = 0;

    /// A wrapper of the above method.
    /**
     * Users can set the timeout value as follows:
     *   app().setIdleConnectionTimeout(0.5h);
     *   app().setIdleConnectionTimeout(30min);
     */
    inline HttpAppFramework &setIdleConnectionTimeout(
        const std::chrono::duration<long double> &timeout)
    {
        return setIdleConnectionTimeout((size_t)timeout.count());
    }

    /// Set the 'server' header field in each response sent by drogon.
    /**
     * @param server: empty string by default with which the 'server' header
     * field is set to "Server: drogon/version string\r\n"
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setServerHeaderField(
        const std::string &server) = 0;

    /// Control if the 'Server' header or the 'Date' header is added to each
    /// HTTP response.
    /**
     * NOTE:
     * These operations can be performed by options in the configuration file.
     * The headers are sent to clients by default.
     */
    virtual HttpAppFramework &enableServerHeader(bool flag) = 0;
    virtual HttpAppFramework &enableDateHeader(bool flag) = 0;

    /// Set the maximum number of requests that can be served through one
    /// keep-alive connection.
    /**
     * After the maximum number of requests are made, the connection is closed.
     * The default value is 0 which means no limit.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setKeepaliveRequestsNumber(
        const size_t number) = 0;

    /// Set the maximum number of unhandled requests that can be cached in
    /// pipelining buffer.
    /**
     * The default value of 0 means no limit.
     * After the maximum number of requests cached in pipelining buffer are
     * made, the connection is closed.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setPipeliningRequestsNumber(
        const size_t number) = 0;

    /// Set the gzip_static option.
    /**
     * If it is set to true, when the client requests a static file, drogon
     * first finds the compressed file with the extension ".gz" in the same path
     * and send the compressed file to the client. The default value is true.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setGzipStatic(bool useGzipStatic) = 0;

    /// Set the max body size of the requests received by drogon. The default
    /// value is 1M.
    /**
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setClientMaxBodySize(size_t maxSize) = 0;

    /// Set the maximum body size in memory of HTTP requests received by drogon.
    /**
     * The default value is "64K" bytes. If the body size of a HTTP request
     * exceeds this limit, the body is stored to a temporary file for
     * processing.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setClientMaxMemoryBodySize(size_t maxSize) = 0;

    /// Set the max size of messages sent by WebSocket client. The default value
    /// is 128K.
    /**
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setClientMaxWebSocketMessageSize(
        size_t maxSize) = 0;

    // Set the HTML file of the home page, the default value is "index.html"
    /**
     * If there isn't any handler registered to the path "/", the home page file
     * in the "document_root"
     * is send to clients as a response to the request for "/".
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &setHomePage(const std::string &homePageFile) = 0;

    /// Get a database client by @param name
    /**
     * NOTE:
     * This method must be called after the framework has been run.
     */
    virtual orm::DbClientPtr getDbClient(
        const std::string &name = "default") = 0;

    /// Get a 'fast' database client by @param name
    /**
     * NOTE:
     * This method must be called after the framework has been run.
     */
    virtual orm::DbClientPtr getFastDbClient(
        const std::string &name = "default") = 0;

    /// Create a database client
    /**
     * @param dbType: The database type is one of
     * "postgresql","mysql","sqlite3".
     * @param host: IP or host name.
     * @param port: The port on which the database server is listening.
     * @databaseName, @param userName, @param password: ...
     * @connectionNum: The number of connections to the database server. It's
     * valid only if @param isFast is false.
     * @filename: The file name of sqlite3 database file.
     * @name: The client name.
     * @isFast: Indicates if the client is a fast database client.
     *
     * NOTE:
     * This operation can be performed by an option in the configuration file.
     */
    virtual HttpAppFramework &createDbClient(
        const std::string &dbType,
        const std::string &host,
        const u_short port,
        const std::string &databaseName,
        const std::string &userName,
        const std::string &password,
        const size_t connectionNum = 1,
        const std::string &filename = "",
        const std::string &name = "default",
        const bool isFast = false) = 0;

    /// Get the DNS resolver
    /**
     * NOTE:
     * When the c-ares library is installed in the system, it runs with the best
     * performance.
     */
    virtual const std::shared_ptr<trantor::Resolver> &getResolver() const = 0;

    virtual bool supportSSL() const = 0;

  private:
    virtual void registerHttpController(
        const std::string &pathPattern,
        const internal::HttpBinderBasePtr &binder,
        const std::vector<HttpMethod> &validMethods = std::vector<HttpMethod>(),
        const std::vector<std::string> &filters = std::vector<std::string>(),
        const std::string &handlerName = "") = 0;
};

inline HttpAppFramework &app()
{
    return HttpAppFramework::instance();
}

}  // namespace drogon
