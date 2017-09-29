// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#include <algorithm>
#include "pipeline.h"
#include "stream.h"
#include "media/record/record_device.h"
#include "media/ros/ros_writer.h"

namespace librealsense
{
    /*

      ______   ______   .__   __.  _______  __    _______
     /      | /  __  \  |  \ |  | |   ____||  |  /  _____|
    |  ,----'|  |  |  | |   \|  | |  |__   |  | |  |  __
    |  |     |  |  |  | |  . `  | |   __|  |  | |  | |_ |
    |  `----.|  `--'  | |  |\   | |  |     |  | |  |__| |
     \______| \______/  |__| \__| |__|     |__|  \______|

    */

    void pipeline_config::enable_stream(rs2_stream stream, int index, int width, int height, rs2_format format, int fps)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _stream_requests[{stream, index}] = { stream, index, width, height, format, fps };
    }

    void pipeline_config::enable_all_stream()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _stream_requests.clear();
        _enable_all_streams = true;
    }

    void pipeline_config::enable_device(const std::string& serial)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _device_request.serial = serial;
    }

    void pipeline_config::enable_device_from_file(const std::string& file)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (!_device_request.record_output.empty())
        {
            throw std::runtime_error("Configuring both device from file, and record to file is unsupported");
        }
        _device_request.filename = file;
    }

    void pipeline_config::enable_record_to_file(const std::string& file)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (!_device_request.filename.empty())
        {
            throw std::runtime_error("Configuring both device from file, and record to file is unsupported");
        }
        _device_request.record_output = file;
    }

    void pipeline_config::disable_stream(rs2_stream stream)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto itr = std::begin(_stream_requests);
        while (itr != std::end(_stream_requests)) 
        {
            if (itr->first.first == stream)
            {
                itr = _stream_requests.erase(itr);
            }
            else 
            {
                ++itr;
            }
        }
    }

    void pipeline_config::disable_all_streams()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _stream_requests.clear();
        _enable_all_streams = false;
    }

    std::shared_ptr<pipeline_profile> pipeline_config::resolve(std::shared_ptr<pipeline> pipe)
    {
        std::lock_guard<std::mutex> lock(_mtx);

        auto requested_device = resolve_device_requests(pipe);

        std::vector<stream_profile> resolved_profiles;

        if (_enable_all_streams)
        {
            if (!requested_device)
            {
                requested_device = get_first_or_default_device(pipe);
            }

            util::config config;
            config.enable_all(util::best_quality);
            auto multistream = config.resolve(requested_device.get());
            return std::make_shared<pipeline_profile>(requested_device, multistream, _device_request.record_output);
        }
        else
        {
            util::config config;

            //If the user did not request anything, give it the default
            if (_stream_requests.empty())
            {
                if (!requested_device)
                {
                    requested_device = get_first_or_default_device(pipe);
                }

                auto default_profiles = get_default_configuration(requested_device);
                for (auto prof : default_profiles)
                {
                    auto p = dynamic_cast<video_stream_profile*>(prof.get());
                    if (!p)
                    {
                        throw std::runtime_error(to_string() << "stream_profile is not video_stream_profile");
                    }
                    config.enable_stream(p->get_stream_type(), p->get_stream_index(), p->get_width(), p->get_height(), p->get_format(), p->get_framerate());
                }

                auto multistream = config.resolve(requested_device.get());
                return std::make_shared<pipeline_profile>(requested_device, multistream, _device_request.record_output);
            }
            else
            {
                //User enabled some stream, enable only them   
                for(auto&& req : _stream_requests)
                {
                    auto r = req.second;
                    config.enable_stream(r.stream, r.stream_index, r.width, r.height, r.format, r.fps);
                }

                for (auto dev_info : pipe->get_context()->query_devices())
                {
                    try
                    {
                        auto dev = dev_info->create_device();
                        auto multistream = config.resolve(dev.get());
                        return std::make_shared<pipeline_profile>(dev, multistream, _device_request.record_output);
                    }
                    catch (...) {}
                }

                throw std::runtime_error("Config couldn't configure pipeline");

            }
        }
    }

    bool pipeline_config::can_resolve(std::shared_ptr<pipeline> pipe)
    {
        throw not_implemented_exception(__FUNCTION__);
    }
    std::shared_ptr<device_interface> pipeline_config::get_first_or_default_device(std::shared_ptr<pipeline> pipe)
    {
        try
        {
            return pipe->wait_for_device(5000);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(to_string() << "Failed to resolve request. " << e.what());
        }
    }
    //TODO: add timeout parameter?
    std::shared_ptr<device_interface> pipeline_config::resolve_device_requests(std::shared_ptr<pipeline> pipe)
    {
        const auto timeout_ms = std::chrono::milliseconds(5000).count();

        //Prefer filename over serial
        if(!_device_request.filename.empty())
        {
            std::shared_ptr<device_interface> dev;
            try
            {
                dev = pipe->get_context()->add_device(_device_request.filename);
            }
            catch(const std::exception& e)
            {
                throw std::runtime_error(to_string() << "Failed to resolve request. Request to enable_device_from_file(\"" << _device_request.filename << "\") was invalid, Reason: " << e.what());
            }
            //check if a serial number was also requested, and check again the device
            if (!_device_request.serial.empty())
            {
                if (!dev->supports_info(RS2_CAMERA_INFO_SERIAL_NUMBER))
                {
                    throw std::runtime_error(to_string() << "Failed to resolve request. "
                        "Conflic between enable_device_from_file(\"" << _device_request.filename
                        << "\") and enable_device(\"" << _device_request.serial << "\"), "
                        "File does not contain a device with such serial");
                }
                else
                {
                    std::string s = dev->get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
                    if (s != _device_request.serial)
                    {
                        throw std::runtime_error(to_string() << "Failed to resolve request. "
                            "Conflic between enable_device_from_file(\"" << _device_request.filename 
                             << "\") and enable_device(\"" << _device_request.serial << "\"), "
                            "File contains device with different serial number (" << s << "\")");
                    }
                }
            }
            return dev;
        }

        if (!_device_request.serial.empty())
        {
            return pipe->wait_for_device(timeout_ms, _device_request.serial);
        }

        return nullptr;
    }

    stream_profiles pipeline_config::get_default_configuration(std::shared_ptr<device_interface> dev)
    {
        stream_profiles default_profiles;

        for (unsigned int i = 0; i < dev->get_sensors_count(); i++)
        {
            auto&& sensor = dev->get_sensor(i);
            auto profiles = sensor.get_stream_profiles();

            for (auto p : profiles)
            {
                if (p->is_default())
                {
                    default_profiles.push_back(p);
                }
            }
        }

        // Workaround - default profiles that holds color stream shouldn't supposed to provide infrared either
        auto color_it = std::find_if(default_profiles.begin(), default_profiles.end(), [](std::shared_ptr<stream_profile_interface> p)
                        {
                            return p.get()->get_stream_type() == RS2_STREAM_COLOR;
                        });

        bool default_profiles_contains_color_stream = color_it != default_profiles.end();
        if (default_profiles_contains_color_stream)
        {
            auto it = std::find_if(default_profiles.begin(), default_profiles.end(), [](std::shared_ptr<stream_profile_interface> p) {return p.get()->get_stream_type() == RS2_STREAM_INFRARED; });
            if (it != default_profiles.end())
            {
                default_profiles.erase(it);
            }
        }

        return default_profiles;
    }

    /*
        .______    __  .______    _______  __       __  .__   __.  _______ 
        |   _  \  |  | |   _  \  |   ____||  |     |  | |  \ |  | |   ____|
        |  |_)  | |  | |  |_)  | |  |__   |  |     |  | |   \|  | |  |__   
        |   ___/  |  | |   ___/  |   __|  |  |     |  | |  . `  | |   __|  
        |  |      |  | |  |      |  |____ |  `----.|  | |  |\   | |  |____ 
        | _|      |__| | _|      |_______||_______||__| |__| \__| |_______|                                                        
    */

    template<class T>
    class internal_frame_callback : public rs2_frame_callback
    {
        T on_frame_function;
    public:
        explicit internal_frame_callback(T on_frame) : on_frame_function(on_frame) {}

        void on_frame(rs2_frame* fref) override
        {
            on_frame_function((frame_interface*)(fref));
        }

        void release() override { delete this; }
    };

    pipeline::pipeline(std::shared_ptr<librealsense::context> ctx)
        :_ctx(ctx), _hub(ctx)
    {}

    pipeline::~pipeline()
    {
        try
        {
            stop();
        }
        catch (...) {}
    }

    std::shared_ptr<pipeline_profile> pipeline::start(std::shared_ptr<pipeline_config> conf)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_active_profile)
        {
            throw librealsense::wrong_api_call_sequence_exception("start() cannnot be called before stop()");
        }
        unsafe_start(conf);
        return get_active_profile();
    }

    std::shared_ptr<pipeline_profile> pipeline::start_with_record(std::shared_ptr<pipeline_config> conf, const std::string& file)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_active_profile)
        {
            throw librealsense::wrong_api_call_sequence_exception("start() cannnot be called before stop()");
        }
        conf->enable_record_to_file(file);
        unsafe_start(conf);
        return get_active_profile();
    }

    std::shared_ptr<pipeline_profile> pipeline::get_active_profile() const
    {
        return _active_profile;
    }

    void pipeline::unsafe_start(std::shared_ptr<pipeline_config> conf)
    {
        auto syncer = std::unique_ptr<syncer_proccess_unit>(new syncer_proccess_unit());
        auto queue = std::unique_ptr<single_consumer_queue<frame_holder>>(new single_consumer_queue<frame_holder>());

        auto to_user = [&](frame_holder fref)
        {
            _queue->enqueue(std::move(fref));
        };

        frame_callback_ptr user_callback =
        { new internal_frame_callback<decltype(to_user)>(to_user),
            [](rs2_frame_callback* p) { p->release(); } };

        auto to_syncer = [&](frame_holder fref)
        {
            _syncer->invoke(std::move(fref));
        };

        frame_callback_ptr syncer_callback =
        { new internal_frame_callback<decltype(to_syncer)>(to_syncer),
            [](rs2_frame_callback* p) { p->release(); } };


        syncer->set_output_callback(user_callback);

        _queue = std::move(queue);
        _syncer = std::move(syncer);

        std::shared_ptr<pipeline_profile> profile = nullptr;
        const int NUM_TIMES_TO_RETRY = 3;
        for (int i = 1; i <= NUM_TIMES_TO_RETRY; i++)
        {
            try
            {
                profile = conf->resolve(shared_from_this());
                profile->_multistream.open();
                profile->_multistream.start(syncer_callback);
                break;
            }
            catch (...)
            {
                if (i == NUM_TIMES_TO_RETRY)
                    throw;
            }
        }

        //On successfull start, update members:
        _active_profile = profile;
        _prev_conf = conf;
    }

    void pipeline::stop()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (!_active_profile)
        {
            throw librealsense::wrong_api_call_sequence_exception("stop() cannnot be called before start()");
        }
        _active_profile->_multistream.stop();
        _active_profile->_multistream.close();
        _syncer.reset();
        _queue.reset();
        _active_profile.reset();
    }

    frame_holder pipeline::wait_for_frames(unsigned int timeout_ms)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (!_active_profile)
        {
            throw librealsense::wrong_api_call_sequence_exception("wait_for_frames cannnot be called before start()");
        }

        frame_holder f;
        if (_queue->dequeue(&f, timeout_ms))
        {
            return f;
        }
        
        if (!_hub.is_connected(*_active_profile->get_device()))
        {
            //_dev = _hub.wait_for_device(timeout_ms/*, _dev.get()*/);
            //TODO: shouw we use active_profile to start with a new config?
            unsafe_start(_prev_conf);
            return frame_holder();
        }
        else
        {
            throw std::runtime_error(to_string() << "Frame didn't arrived within " << timeout_ms);
        }
        
    }

    bool pipeline::poll_for_frames(frame_holder* frame)
    {
        std::lock_guard<std::mutex> lock(_mtx);

        if (!_active_profile)
        {
            throw librealsense::wrong_api_call_sequence_exception("poll_for_frames cannnot be called before start()");
        }

        if (_queue->try_dequeue(frame))
        {
            return true;
        }
        return false;
    }

    std::shared_ptr<device_interface> pipeline::wait_for_device(unsigned int timeout_ms, std::string serial)
    {
        return _hub.wait_for_device(timeout_ms, serial);
    }

    std::shared_ptr<librealsense::context> pipeline::get_context() const
    {
        return _ctx;
    }


    /*
        .______   .______        ______    _______  __   __       _______
        |   _  \  |   _  \      /  __  \  |   ____||  | |  |     |   ____|
        |  |_)  | |  |_)  |    |  |  |  | |  |__   |  | |  |     |  |__
        |   ___/  |      /     |  |  |  | |   __|  |  | |  |     |   __|
        |  |      |  |\  \----.|  `--'  | |  |     |  | |  `----.|  |____
        | _|      | _| `._____| \______/  |__|     |__| |_______||_______|
    */

    pipeline_profile::pipeline_profile(std::shared_ptr<device_interface> dev, 
                                       util::config::multistream multistream, 
                                       const std::string& to_file) :
        _dev(dev), _multistream(multistream), _to_file(to_file)
    {
        if (!to_file.empty())
        {
            if (!dev)
                throw librealsense::invalid_value_exception("Failed to create a pipeline_profile, device is null");

            _dev = std::make_shared<record_device>(dev, std::make_shared<ros_writer>(to_file));
        }
    }

    std::shared_ptr<device_interface> pipeline_profile::get_device()
    {
        //pipeline_profile can be retrieved from a pipeline_config and pipeline::start()
        //either way, it is created by the pipeline
        
        //TODO: handle case where device has disconnected and reconnected
        //TODO: remember to recreate the device as record device in case of to_file.empty() == false
        if (!_dev)
        {
            throw std::runtime_error("Device is unavailable");
        }
        return _dev;
    }

    stream_profiles pipeline_profile::get_active_streams() const
    {
        auto profiles_per_sensor = _multistream.get_profiles_per_sensor();
        stream_profiles profiles;
        for (auto&& kvp : profiles_per_sensor)
            for (auto&& p : kvp.second)
                profiles.push_back(p);

        return profiles;
    }
}
