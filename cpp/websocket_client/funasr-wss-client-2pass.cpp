/**
 * Copyright FunASR (https://github.com/alibaba-damo-academy/FunASR). All Rights
 * Reserved. MIT License  (https://opensource.org/licenses/MIT)
 */
/* 2022-2023 by zhaomingwork */

// client for websocket, support multiple threads
// ./funasr-wss-client  --server-ip <string>
//                     --port <string>
//                     --wav-path <string>
//                     [--thread-num <int>] 
//                     [--is-ssl <int>]  [--]
//                     [--version] [-h]
// example:
// ./funasr-wss-client --server-ip 127.0.0.1 --port 10095 --wav-path test.wav --thread-num 1 --is-ssl 1

#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <fstream>
#include <atomic>
#include <thread>


#include "audio.h"
#include "nlohmann/json.hpp"
#include "tclap/CmdLine.h"

/**
 * Define a semi-cross platform helper method that waits/sleeps for a bit.
 */
void WaitABit() {
    #ifdef WIN32
        Sleep(1000);
    #else
        usleep(1000);
    #endif
}
std::atomic<int> wav_index(0);

bool IsTargetFile(const std::string& filename, const std::string target) {
    std::size_t pos = filename.find_last_of(".");
    if (pos == std::string::npos) {
        return false;
    }
    std::string extension = filename.substr(pos + 1);
    return (extension == target);
}

typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
context_ptr OnTlsInit(websocketpp::connection_hdl) {
    context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(
        asio::ssl::context::sslv23);

    try {
        ctx->set_options(
            asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
            asio::ssl::context::no_sslv3 | asio::ssl::context::single_dh_use);

    } catch (std::exception& e) {
        cout << e.what() << endl;
    }
    return ctx;
}

// template for tls or not config
template <typename T>
class WebsocketClient {
  public:
    // typedef websocketpp::client<T> client;
    // typedef websocketpp::client<websocketpp::config::asio_tls_client>
    // wss_client;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;

    WebsocketClient(int is_ssl) : m_open(false), m_done(false) {
        // set up access channels to only log interesting things
        m_client.clear_access_channels(websocketpp::log::alevel::all);
        m_client.set_access_channels(websocketpp::log::alevel::connect);
        m_client.set_access_channels(websocketpp::log::alevel::disconnect);
        m_client.set_access_channels(websocketpp::log::alevel::app);

        // Initialize the Asio transport policy
        m_client.init_asio();

        // Bind the handlers we are using
        using websocketpp::lib::bind;
        using websocketpp::lib::placeholders::_1;
        m_client.set_open_handler(bind(&WebsocketClient::on_open, this, _1));
        m_client.set_close_handler(bind(&WebsocketClient::on_close, this, _1));

        m_client.set_message_handler(
            [this](websocketpp::connection_hdl hdl, message_ptr msg) {
              on_message(hdl, msg);
            });

        m_client.set_fail_handler(bind(&WebsocketClient::on_fail, this, _1));
        m_client.clear_access_channels(websocketpp::log::alevel::all);
    }

    void on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
        const std::string& payload = msg->get_payload();
        switch (msg->get_opcode()) {
            case websocketpp::frame::opcode::text:
                nlohmann::json jsonresult = nlohmann::json::parse(payload);
                cout << "Thread: " << this_thread::get_id() <<",on_message = " << payload << endl;
        if (jsonresult["is_final"] == true) {
          websocketpp::lib::error_code ec;
           
          m_client.close(hdl, websocketpp::close::status::going_away, "", ec);

          if (ec) {
            cout << "Error closing connection " << ec.message() << endl;
          }
        }
    }
  }

    // This method will block until the connection is complete  
    void run(const std::string& uri, const std::vector<string>& wav_list, const std::vector<string>& wav_ids, std::string asr_mode, std::vector<int> chunk_size) {
        // Create a new connection to the given URI
        websocketpp::lib::error_code ec;
        typename websocketpp::client<T>::connection_ptr con =
            m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                                    "Get Connection Error: " + ec.message());
            return;
        }
        // Grab a handle for this connection so we can talk to it in a thread
        // safe manor after the event loop starts.
        m_hdl = con->get_handle();

        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);

        // Create a thread to run the ASIO io_service event loop
        websocketpp::lib::thread asio_thread(&websocketpp::client<T>::run,
                                            &m_client);

        send_wav_data(wav_list[0], wav_ids[0], asr_mode, chunk_size);
  
        WaitABit(); 

        asio_thread.join();

    }

    // The open handler will signal that we are ready to start sending data
    void on_open(websocketpp::connection_hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
                                "Connection opened, starting data!");

        scoped_lock guard(m_lock);
        m_open = true;
    }

    // The close handler will signal that we should stop sending data
    void on_close(websocketpp::connection_hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
                                  "Connection closed, stopping data!");

        scoped_lock guard(m_lock);
        m_done = true;
    }

    // The fail handler will signal that we should stop sending data
    void on_fail(websocketpp::connection_hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
                                  "Connection failed, stopping data!");

        scoped_lock guard(m_lock);
        m_done = true;
    }
    // send wav to server
    void send_wav_data(string wav_path, string wav_id, std::string asr_mode, std::vector<int> chunk_vector) {
        uint64_t count = 0;
        std::stringstream val;

		funasr::Audio audio(1);
        int32_t sampling_rate = 16000;
        std::string wav_format = "pcm";
		if(IsTargetFile(wav_path.c_str(), "wav")){
			int32_t sampling_rate = -1;
			if(!audio.LoadWav(wav_path.c_str(), &sampling_rate))
				return ;
		}else if(IsTargetFile(wav_path.c_str(), "pcm")){
			if (!audio.LoadPcmwav(wav_path.c_str(), &sampling_rate))
				return ;
		}else{
			printf("Wrong wav extension");
			exit(-1);
		}

        float* buff;
        int len;
        int flag = 0;
        bool wait = false;
        while (1) {
            {
                scoped_lock guard(m_lock);
                // If the connection has been closed, stop generating data
                if (m_done) {
                  break;
                }
                // If the connection hasn't been opened yet wait a bit and retry
                if (!m_open) {
                  wait = true;
                } else {
                  break;
                }
            }
            if (wait) {
                // LOG(INFO) << "wait.." << m_open;
                WaitABit();
                continue;
            }
        }
        websocketpp::lib::error_code ec;

        nlohmann::json jsonbegin;
        nlohmann::json chunk_size = nlohmann::json::array();
        chunk_size.push_back(chunk_vector[0]);
        chunk_size.push_back(chunk_vector[1]);
        chunk_size.push_back(chunk_vector[2]);
        jsonbegin["mode"] = asr_mode;
        jsonbegin["chunk_size"] = chunk_size;
        jsonbegin["wav_name"] = wav_id;
        jsonbegin["wav_format"] = wav_format;
        jsonbegin["is_speaking"] = true;
        m_client.send(m_hdl, jsonbegin.dump(), websocketpp::frame::opcode::text,
                      ec);

        // fetch wav data use asr engine api
        if(wav_format == "pcm"){
            while (audio.Fetch(buff, len, flag) > 0) {
                short* iArray = new short[len];
                for (size_t i = 0; i < len; ++i) {
                iArray[i] = (short)(buff[i]*32768);
                }

                // send data to server
                int offset = 0;
                int block_size = 102400;
                while(offset < len){
                    int send_block = 0;
                    if (offset + block_size <= len){
                        send_block = block_size;
                    }else{
                        send_block = len - offset;
                    }
                    m_client.send(m_hdl, iArray+offset, send_block * sizeof(short),
                        websocketpp::frame::opcode::binary, ec);
                    offset += send_block;
                }

                cout << "sended data len=" << len * sizeof(short) << endl;
                // The most likely error that we will get is that the connection is
                // not in the right state. Usually this means we tried to send a
                // message to a connection that was closed or in the process of
                // closing. While many errors here can be easily recovered from,
                // in this simple example, we'll stop the data loop.
                if (ec) {
                  m_client.get_alog().write(websocketpp::log::alevel::app,
                                            "Send Error: " + ec.message());
                  break;
                }
                delete[] iArray;
                // WaitABit();
            }
        }else{
            int offset = 0;
            int block_size = 204800;
            len = audio.GetSpeechLen();
            char* others_buff = audio.GetSpeechChar();

            while(offset < len){
                int send_block = 0;
                if (offset + block_size <= len){
                    send_block = block_size;
                }else{
                    send_block = len - offset;
                }
                m_client.send(m_hdl, others_buff+offset, send_block,
                    websocketpp::frame::opcode::binary, ec);
                offset += send_block;
            }

            cout << "sended data len=" << len << endl;
            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the data loop.
            if (ec) {
                m_client.get_alog().write(websocketpp::log::alevel::app,
                                        "Send Error: " + ec.message());
            }
        }

    nlohmann::json jsonresult;
    jsonresult["is_speaking"] = false;
    m_client.send(m_hdl, jsonresult.dump(), websocketpp::frame::opcode::text,
                  ec);
    WaitABit();
  }
  websocketpp::client<T> m_client;

  private:
    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::mutex m_lock;
    bool m_open;
    bool m_done;
	int total_num=0;
};

int main(int argc, char* argv[]) {

    TCLAP::CmdLine cmd("funasr-wss-client-2pass", ' ', "1.0");
    TCLAP::ValueArg<std::string> server_ip_("", "server-ip", "server-ip", true,
                                           "127.0.0.1", "string");
    TCLAP::ValueArg<std::string> port_("", "port", "port", true, "10095", "string");
    TCLAP::ValueArg<std::string> wav_path_("", "wav-path", 
        "the input could be: wav_path, e.g.: asr_example.wav; pcm_path, e.g.: asr_example.pcm; wav.scp, kaldi style wav list (wav_id \t wav_path)", 
        true, "", "string");
    TCLAP::ValueArg<std::string>    asr_mode_("", "mode", "offline, online, 2pass", false, "2pass", "string");
    TCLAP::ValueArg<std::string>    chunk_size_("", "chunk-size", "chunk_size: 5-10-5 or 5-12-5", false, "5-10-5", "string");
    TCLAP::ValueArg<int> thread_num_("", "thread-num", "thread-num",
                                       false, 1, "int");
    TCLAP::ValueArg<int> is_ssl_(
        "", "is-ssl", "is-ssl is 1 means use wss connection, or use ws connection", 
        false, 1, "int");

    cmd.add(server_ip_);
    cmd.add(port_);
    cmd.add(wav_path_);
    cmd.add(asr_mode_);
    cmd.add(chunk_size_);
    cmd.add(thread_num_);
    cmd.add(is_ssl_);
    cmd.parse(argc, argv);

    std::string server_ip = server_ip_.getValue();
    std::string port = port_.getValue();
    std::string wav_path = wav_path_.getValue();
    std::string asr_mode = asr_mode_.getValue();
    std::string chunk_size_str = chunk_size_.getValue();
    // get chunk_size
    std::vector<int> chunk_size;
    std::stringstream ss(chunk_size_str);
    std::string item;   
    while (std::getline(ss, item, '-')) {
        try {
            chunk_size.push_back(stoi(item));
        } catch (const invalid_argument&) {
            cout << "Invalid argument: " << item << endl;
            exit(-1);
        }
    }

    int threads_num = thread_num_.getValue();
    int is_ssl = is_ssl_.getValue();

  std::string uri = "";
  if (is_ssl == 1) {
    uri = "wss://" + server_ip + ":" + port;
  } else {
    uri = "ws://" + server_ip + ":" + port;
  }

  // read wav_path
  std::vector<string> wav_list;
  std::vector<string> wav_ids;
  string default_id = "wav_default_id";
  if (IsTargetFile(wav_path, "scp")) {
    ifstream in(wav_path);
    if (!in.is_open()) {
      printf("Failed to open scp file");
      return 0;
    }
    string line;
    while (getline(in, line)) {
      istringstream iss(line);
      string column1, column2;
      iss >> column1 >> column2;
      wav_list.emplace_back(column2);
      wav_ids.emplace_back(column1);
    }
    in.close();
  } else {
    wav_list.emplace_back(wav_path);
    wav_ids.emplace_back(default_id);
  }

  for (size_t wav_i = 0; wav_i < wav_list.size(); wav_i = wav_i + threads_num) {
    std::vector<websocketpp::lib::thread> client_threads;
    for (size_t i = 0; i < threads_num; i++) {
      if (wav_i + i >= wav_list.size()) {
        break;
      }
      std::vector<string> tmp_wav_list;
      std::vector<string> tmp_wav_ids;

      tmp_wav_list.emplace_back(wav_list[wav_i + i]);
      tmp_wav_ids.emplace_back(wav_ids[wav_i + i]);

      client_threads.emplace_back(
          [uri, tmp_wav_list, tmp_wav_ids, asr_mode, chunk_size, is_ssl]() {
            if (is_ssl == 1) {
              WebsocketClient<websocketpp::config::asio_tls_client> c(is_ssl);

              c.m_client.set_tls_init_handler(bind(&OnTlsInit, ::_1));

              c.run(uri, tmp_wav_list, tmp_wav_ids, asr_mode, chunk_size);
            } else {
              WebsocketClient<websocketpp::config::asio_client> c(is_ssl);

              c.run(uri, tmp_wav_list, tmp_wav_ids, asr_mode, chunk_size);
            }
          });
    }

    for (auto& t : client_threads) {
      t.join();
    }
  }
}
