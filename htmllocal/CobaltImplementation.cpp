/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Module.h"
#include <interfaces/IMemory.h>
#include <interfaces/IBrowser.h>
#include <syslog.h>

extern "C" {

int  StarboardMain(int argc, char **argv);
void SbRdkHandleDeepLink(const char* link);
void SbRdkSuspend();
void SbRdkResume();
void SbRdkQuit();
void SbRdkHandleUserPageJump(const char* link);

}  // extern "C"

namespace WPEFramework {

namespace Plugin {

static void SetThunderAccessPointIfNeeded() {
  const std::string envName = _T("THUNDER_ACCESS");
  std::string envVal;
  if (Core::SystemInfo::GetEnvironment(envName, envVal))
    return;

  Core::File file("/etc/WPEFramework/config.json", false);
  if (!file.Open(true))
    return;

  JsonObject config;
  if (config.IElement::FromFile(file)) {
    Core::JSON::String port = config.Get("port");
    Core::JSON::String binding = config.Get("binding");
    envVal = binding.Value() + ":" + port.Value();
    Core::SystemInfo::SetEnvironment(envName, envVal);
  }
  file.Close();
}

class CobaltHtmlAppImplementation:
    public Exchange::IBrowser,
    public PluginHost::IStateControl {
private:
  class Config: public Core::JSON::Container {
  private:
    Config(const Config&);
    Config& operator=(const Config&);

  public:
    Config() :
      Core::JSON::Container(), Url() {
      Add(_T("url"), &Url);
      Add(_T("web_file_path"), &WebFilePath);
      Add(_T("clientidentifier"), &ClientIdentifier);
      Add(_T("enrironment"), &EnvVars);
      Add(_T("cmdline"), &CmdLine);
    }
    ~Config() {
    }

  public:
    Core::JSON::String Url;
    Core::JSON::String WebFilePath;
    Core::JSON::String ClientIdentifier;
    JsonObject EnvVars;
    JsonArray CmdLine;
  };

  class NotificationSink: public Core::Thread {
  private:
    NotificationSink() = delete;
    NotificationSink(const NotificationSink&) = delete;
    NotificationSink& operator=(const NotificationSink&) = delete;

  public:
    NotificationSink(CobaltHtmlAppImplementation &parent) :
      _parent(parent), _waitTime(0), _command(
        PluginHost::IStateControl::SUSPEND) {
    }
    virtual ~NotificationSink() {
      Stop();
      Wait(Thread::STOPPED | Thread::BLOCKED, Core::infinite);
    }

  public:
    void RequestForStateChange(
      const PluginHost::IStateControl::command command) {
      _lock.Lock();
      _command = command;
      _lock.Unlock();
      Run();
    }

  private:
    virtual uint32_t Worker() {
      bool success = false;

      _lock.Lock();
      const PluginHost::IStateControl::command command = _command;
      _lock.Unlock();

      if ((IsRunning() == true) && (success == false)) {
        success = _parent.RequestForStateChange(command);
      }
      Block();
      _parent.StateChangeCompleted(success, command);

      if (success == true) {
        _lock.Lock();
        bool completed = (command == _command);
        _lock.Unlock();
        // Spin one more time
        if (!completed)
          Run();
      }

      return (Core::infinite);
    }

  private:
    CobaltHtmlAppImplementation &_parent;
    uint32_t _waitTime;
    PluginHost::IStateControl::command _command;
    mutable Core::CriticalSection _lock;
  };

  class CobaltHtmlAppWindow : public Core::Thread {
  private:
    CobaltHtmlAppWindow(const CobaltHtmlAppWindow&) = delete;
    CobaltHtmlAppWindow& operator=(const CobaltHtmlAppWindow&) = delete;

  public:
    CobaltHtmlAppWindow(CobaltHtmlAppImplementation& parent)
      : Core::Thread(0, _T("CobaltHtmlApp"))
      , _url("https://www.youtube.com/tv")
      , _web_file_path()
      , _parent(parent)
    {
    }
    virtual ~CobaltHtmlAppWindow()
    {
      Block();
      syslog(LOG_INFO, "Ness_Debug: ask CobaltHtmlApp to exit\n");
      SbRdkQuit();
      Wait(Thread::BLOCKED | Thread::STOPPED | Thread::STOPPING, Core::infinite);
      //exit(_exitCode);
      syslog(LOG_INFO, "Ness_Debug: ~CobaltHtmlAppWindow exit done\n");
    }

    uint32_t Configure(PluginHost::IShell* service) {
      uint32_t result = Core::ERROR_NONE;

      Config config;
      config.FromString(service->ConfigLine());
      syslog(LOG_ERR, "Ness CobaltHtmlApp::Implement ConfigLine = %s\n",
        service->ConfigLine().c_str());
      Core::Directory(service->PersistentPath().c_str()).CreatePath();
      Core::SystemInfo::SetEnvironment(_T("HOME"), service->PersistentPath());
      Core::SystemInfo::SetEnvironment(_T("COBALT_TEMP"), service->VolatilePath());
      if (config.ClientIdentifier.IsSet() == true) {
        string value(service->Callsign() + ',' + config.ClientIdentifier.Value());
        Core::SystemInfo::SetEnvironment(_T("CLIENT_IDENTIFIER"), value);
        Core::SystemInfo::SetEnvironment(_T("WAYLAND_DISPLAY"), config.ClientIdentifier.Value());
      } else {
        Core::SystemInfo::SetEnvironment(_T("CLIENT_IDENTIFIER"), service->Callsign());
      }

      SetThunderAccessPointIfNeeded();

      if (config.Url.IsSet() == true) {
        _url = config.Url.Value();
      }

      if (config.WebFilePath.IsSet() == true) {
        _web_file_path = config.WebFilePath.Value();
      }

      if (config.EnvVars.IsSet()) {
        for (JsonObject::Iterator it = config.EnvVars.Variants(); it.Next();) {
          const char *env = it.Label();
          const std::string val = it.Current().String();
          Core::SystemInfo::SetEnvironment(env, val.c_str());
        }
      }
      additional_args = config.CmdLine;
      Run();
      return result;
    }

    bool Suspend(const bool suspend)
    {
      if (suspend == true) {
        SbRdkSuspend();
      }
      else {
        SbRdkResume();
      }
      return (true);
    }

    string Url() const { return _url; }

    string WebFilePath() const { return _web_file_path; }

  private:
    bool Initialize() override
    {
      sigset_t mask;
      sigemptyset(&mask);
      sigaddset(&mask, SIGQUIT);
      sigaddset(&mask, SIGUSR1);
      sigaddset(&mask, SIGCONT);
      pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
      return (true);
    }
    uint32_t Worker() override
    {
      const std::string cmdURL = "--url=" + _url;

      std::vector<const char*> argv;

      argv.push_back("CobaltHtmlApp");
      if (!_url.empty())
        argv.push_back(cmdURL.c_str());

      {
        std::string args_str;
        for (const char* arg: argv) {
          if (!args_str.empty())
            args_str += " ";
          args_str += arg;
        }
        SYSLOG(Logging::Notification, (_T("StarboardMain args: %s\n"), args_str.c_str()));
      }

      std::string cmdWebFilePath = "";
      if (_web_file_path.size() != 0) {
        cmdWebFilePath = "--web_file_path=" + _web_file_path;
      }
      syslog(LOG_INFO, "Ness_Debug: start to launch APP url = %s, web_file_path = %s\n",
          cmdURL.c_str(), cmdWebFilePath.size()!=0?cmdWebFilePath.c_str():"NULL");
      //const char* argv[] = {"CobaltHtmlApp", cmdURL.c_str(), cmdWebFilePath.c_str()};
      argv.push_back(cmdWebFilePath.c_str());

      std::vector<std::string> additional_args_buf;
      for (JsonArray::Iterator it = additional_args.Elements(); it.Next();) {
        additional_args_buf.push_back(it.Current().String());
        argv.push_back(additional_args_buf.back().c_str());
      }

      if (IsRunning() == true) {

          _exitCode = StarboardMain(argv.size(), const_cast<char**>(argv.data()));
          syslog(LOG_INFO, "Ness_Debug: StarboardMain return = %d\n", _exitCode);
      }
      if (IsRunning() == true)  {// app initiated exit
          _parent.StateChange(PluginHost::IStateControl::EXITED);
          syslog(LOG_INFO, "Ness_Debug: notify WPE PluginHost::IStateControl::EXITED\n");
      }
      Block();
      return (Core::infinite);
    }

    int _exitCode { 0 };
    string _url;
    string _web_file_path;
    CobaltHtmlAppImplementation &_parent;
    JsonArray additional_args;
  };

private:
  CobaltHtmlAppImplementation(const CobaltHtmlAppImplementation&) = delete;
  CobaltHtmlAppImplementation& operator=(const CobaltHtmlAppImplementation&) = delete;

public:
  CobaltHtmlAppImplementation() :
    _window(*this),
    _adminLock(),
    _state(PluginHost::IStateControl::UNINITIALIZED),
    _statePending(PluginHost::IStateControl::UNINITIALIZED),
    _cobaltClients(),
    _stateControlClients(),
    _sink(*this) {
      openlog(NULL, LOG_CONS | LOG_PID, LOG_USER);
  }

  virtual ~CobaltHtmlAppImplementation() {
      closelog();
  }

  virtual uint32_t Configure(PluginHost::IShell *service) {
    uint32_t result = _window.Configure(service);
    _state = PluginHost::IStateControl::RESUMED;
    return (result);
  }

  virtual void SetURL(const string &URL) override {
    SbRdkHandleDeepLink(URL.c_str());
  }

  virtual string GetURL() const override {
    return _window.Url();
  }

  virtual uint32_t GetFPS() const override {
    return 0;
  }

  virtual void Hide(const bool hidden) {
  }

  virtual void Register(Exchange::IBrowser::INotification *sink) {
    _adminLock.Lock();

    // Make sure a sink is not registered multiple times.
    ASSERT(
      std::find(_cobaltClients.begin(), _cobaltClients.end(), sink)
      == _cobaltClients.end());

    _cobaltClients.push_back(sink);
    sink->AddRef();

    _adminLock.Unlock();
  }

  virtual void Unregister(Exchange::IBrowser::INotification *sink) {
    _adminLock.Lock();

    std::list<Exchange::IBrowser::INotification*>::iterator index(
      std::find(_cobaltClients.begin(), _cobaltClients.end(), sink));

    // Make sure you do not unregister something you did not register !!!
    ASSERT(index != _cobaltClients.end());

    if (index != _cobaltClients.end()) {
      (*index)->Release();
      _cobaltClients.erase(index);
    }

    _adminLock.Unlock();
  }

  virtual void Register(PluginHost::IStateControl::INotification *sink) {
    _adminLock.Lock();

    // Make sure a sink is not registered multiple times.
    ASSERT(
      std::find(_stateControlClients.begin(),
                _stateControlClients.end(), sink)
      == _stateControlClients.end());

    _stateControlClients.push_back(sink);
    sink->AddRef();

    _adminLock.Unlock();
  }

  virtual void Unregister(PluginHost::IStateControl::INotification *sink) {
    _adminLock.Lock();

    std::list<PluginHost::IStateControl::INotification*>::iterator index(
      std::find(_stateControlClients.begin(),
                _stateControlClients.end(), sink));

    // Make sure you do not unregister something you did not register !!!
    ASSERT(index != _stateControlClients.end());

    if (index != _stateControlClients.end()) {
      (*index)->Release();
      _stateControlClients.erase(index);
    }

    _adminLock.Unlock();
  }

  virtual PluginHost::IStateControl::state State() const {
    return (_state);
  }

  virtual uint32_t Request(const PluginHost::IStateControl::command command) {
    uint32_t result = Core::ERROR_ILLEGAL_STATE;

    _adminLock.Lock();

    if (_state == PluginHost::IStateControl::UNINITIALIZED) {
      // Seems we are passing state changes before we reached an operational CobaltHtmlApp.
      // Just move the state to what we would like it to be :-)
      _state = (
        command == PluginHost::IStateControl::SUSPEND ?
        PluginHost::IStateControl::SUSPENDED :
        PluginHost::IStateControl::RESUMED);
      result = Core::ERROR_NONE;
    } else {
      switch (command) {
        case PluginHost::IStateControl::SUSPEND:
          if (_state == PluginHost::IStateControl::RESUMED || _statePending == PluginHost::IStateControl::RESUMED) {
            _statePending = PluginHost::IStateControl::SUSPENDED;
            _sink.RequestForStateChange(
              PluginHost::IStateControl::SUSPEND);
            result = Core::ERROR_NONE;
          }
          break;
        case PluginHost::IStateControl::RESUME:
          if (_state == PluginHost::IStateControl::SUSPENDED || _statePending == PluginHost::IStateControl::SUSPENDED) {
            _statePending = PluginHost::IStateControl::RESUMED;
            _sink.RequestForStateChange(
              PluginHost::IStateControl::RESUME);
            result = Core::ERROR_NONE;
          }
          break;
        default:
          break;
      }
    }

    _adminLock.Unlock();

    return result;
  }

  void StateChangeCompleted(bool success,
                            const PluginHost::IStateControl::command request) {
    if (success) {
      switch (request) {
        case PluginHost::IStateControl::RESUME:

          _adminLock.Lock();

          if (_state != PluginHost::IStateControl::RESUMED) {
            StateChange(PluginHost::IStateControl::RESUMED);
          }

          _adminLock.Unlock();
          break;
        case PluginHost::IStateControl::SUSPEND:

          _adminLock.Lock();

          if (_state != PluginHost::IStateControl::SUSPENDED) {
            StateChange(PluginHost::IStateControl::SUSPENDED);
          }

          _adminLock.Unlock();
          break;
        default:
          ASSERT(false);
          break;
      }
    } else {
      StateChange(PluginHost::IStateControl::EXITED);
    }
  }

  BEGIN_INTERFACE_MAP (CobaltHtmlAppImplementation)
  INTERFACE_ENTRY (Exchange::IBrowser)
  INTERFACE_ENTRY (PluginHost::IStateControl)
  END_INTERFACE_MAP

  private:
  inline bool RequestForStateChange(
    const PluginHost::IStateControl::command command) {
    bool result = false;

    switch (command) {
      case PluginHost::IStateControl::SUSPEND: {
        if (_window.Suspend(true) == true) {
          result = true;
        }
        break;
      }
      case PluginHost::IStateControl::RESUME: {
        if (_window.Suspend(false) == true) {
          result = true;
        }
        break;
      }
      default:
        ASSERT(false);
        break;
    }
    return result;
  }

  void StateChange(const PluginHost::IStateControl::state newState) {
    _adminLock.Lock();

    _state = newState;
    _statePending = PluginHost::IStateControl::UNINITIALIZED;

    std::list<PluginHost::IStateControl::INotification*>::iterator index(
      _stateControlClients.begin());

    while (index != _stateControlClients.end()) {
      (*index)->StateChange(newState);
      index++;
    }

    _adminLock.Unlock();
  }

private:
  CobaltHtmlAppWindow _window;
  mutable Core::CriticalSection _adminLock;
  PluginHost::IStateControl::state _state;
  PluginHost::IStateControl::state _statePending;
  std::list<Exchange::IBrowser::INotification*> _cobaltClients;
  std::list<PluginHost::IStateControl::INotification*> _stateControlClients;
  NotificationSink _sink;
};

SERVICE_REGISTRATION(CobaltHtmlAppImplementation, 1, 0);

}  // namespace Plugin

namespace CobaltHtmlApp {

class MemoryObserverImpl: public Exchange::IMemory {
private:
  MemoryObserverImpl();
  MemoryObserverImpl(const MemoryObserverImpl&);
  MemoryObserverImpl& operator=(const MemoryObserverImpl&);

public:
  MemoryObserverImpl(const RPC::IRemoteConnection* connection) :
    _main(connection == nullptr ? Core::ProcessInfo().Id() : connection->RemoteId()) {
  }
  ~MemoryObserverImpl() {
  }

public:
  virtual uint64_t Resident() const {
    return _main.Resident();
  }
  virtual uint64_t Allocated() const {
    return _main.Allocated();
  }
  virtual uint64_t Shared() const {
    return _main.Shared();
  }
  virtual uint8_t Processes() const {
    return (IsOperational() ? 1 : 0);
  }

  virtual const bool IsOperational() const {
    return _main.IsActive();
  }

  BEGIN_INTERFACE_MAP (MemoryObserverImpl)
  INTERFACE_ENTRY (Exchange::IMemory)
  END_INTERFACE_MAP

  private:
  Core::ProcessInfo _main;
};

Exchange::IMemory* MemoryObserver(const RPC::IRemoteConnection* connection) {
  ASSERT(connection != nullptr);
  Exchange::IMemory* result = Core::Service<MemoryObserverImpl>::Create<Exchange::IMemory>(connection);
  return (result);
}

}  // namespace CobaltHtmlApp

}  // namespace WPEFramework
