use crate::{
    FfiPose, FfiQuat, DECODER_CONFIG, FILESYSTEM_LAYOUT, SERVER_DATA_MANAGER, VIDEO_MIRROR_SENDER, VIDEO_RECORDING_FILE
};
use alvr_common::{log, prelude::*};
use alvr_events::{Event, EventType};
use alvr_packets::ServerRequest;
use bytes::Buf;
use futures::SinkExt;
use headers::HeaderMapExt;
use hyper::{
    header::{HeaderValue, ACCESS_CONTROL_ALLOW_ORIGIN, CACHE_CONTROL, CONTENT_TYPE},
    service, Body, Request, Response, StatusCode,
};
use serde::de::DeserializeOwned;
use serde_json as json;
use std::net::SocketAddr;
use tokio::sync::broadcast::{self, error::RecvError};
use tokio_tungstenite::{tungstenite::protocol, WebSocketStream};
use tokio_util::codec::{BytesCodec, FramedRead};

pub const WS_BROADCAST_CAPACITY: usize = 256;

fn reply(code: StatusCode) -> StrResult<Response<Body>> {
    Response::builder()
        .status(code)
        .body(Body::empty())
        .map_err(err!())
}

async fn from_request_body<T: DeserializeOwned>(request: Request<Body>) -> StrResult<T> {
    json::from_reader(
        hyper::body::aggregate(request)
            .await
            .map_err(err!())?
            .reader(),
    )
    .map_err(err!())
}

async fn websocket<T: Clone + Send + 'static>(
    request: Request<Body>,
    sender: broadcast::Sender<T>,
    message_builder: impl Fn(T) -> protocol::Message + Send + Sync + 'static,
) -> StrResult<Response<Body>> {
    if let Some(key) = request.headers().typed_get::<headers::SecWebsocketKey>() {
        tokio::spawn(async move {
            match hyper::upgrade::on(request).await {
                Ok(upgraded) => {
                    let mut data_receiver = sender.subscribe();

                    let mut ws =
                        WebSocketStream::from_raw_socket(upgraded, protocol::Role::Server, None)
                            .await;

                    loop {
                        match data_receiver.recv().await {
                            Ok(data) => {
                                if let Err(e) = ws.send(message_builder(data)).await {
                                    info!("Failed to send log with websocket: {e}");
                                    break;
                                }

                                ws.flush().await.ok();
                            }
                            Err(RecvError::Lagged(_)) => {
                                warn!("Some log lines have been lost because the buffer is full");
                            }
                            Err(RecvError::Closed) => break,
                        }
                    }

                    ws.close(None).await.ok();
                }
                Err(e) => error!("{e}"),
            }
        });

        let mut response = Response::builder()
            .status(StatusCode::SWITCHING_PROTOCOLS)
            .body(Body::empty())
            .map_err(err!())?;

        let h = response.headers_mut();
        h.typed_insert(headers::Upgrade::websocket());
        h.typed_insert(headers::SecWebsocketAccept::from(key));
        h.typed_insert(headers::Connection::upgrade());

        Ok(response)
    } else {
        reply(StatusCode::BAD_REQUEST)
    }
}

async fn http_api(
    request: Request<Body>,
    events_sender: broadcast::Sender<Event>,
) -> StrResult<Response<Body>> {
    let mut response = match request.uri().path() {
        // New unified requests
        "/api/dashboard-request" => {
            if let Ok(request) = from_request_body::<ServerRequest>(request).await {
                match request {
                    ServerRequest::Log(event) => {
                        let level = event.severity.into_log_level();
                        log::log!(level, "{}", event.content);
                    }
                    ServerRequest::GetSession => {
                        alvr_events::send_event(EventType::Session(Box::new(
                            SERVER_DATA_MANAGER.read().session().clone(),
                        )));
                    }
                    ServerRequest::UpdateSession(session) => {
                        *SERVER_DATA_MANAGER.write().session_mut() = *session
                    }
                    ServerRequest::SetValues(descs) => {
                        SERVER_DATA_MANAGER.write().set_values(descs).ok();
                    }
                    ServerRequest::UpdateClientList { hostname, action } => SERVER_DATA_MANAGER
                        .write()
                        .update_client_list(hostname, action),
                    ServerRequest::GetAudioDevices => {
                        if let Ok(list) = SERVER_DATA_MANAGER.read().get_audio_devices_list() {
                            alvr_events::send_event(EventType::AudioDevices(list));
                        }
                    }
                    ServerRequest::HmdPoseOffset(PoseOffset,position_lock,roation_lock) =>unsafe {
                        
                    let ffipose_offset = FfiPose{
                        x:PoseOffset.position.x,
                        y:PoseOffset.position.y,
                        z:PoseOffset.position.z,
                        orientation: FfiQuat{
                            x:PoseOffset.orientation.x,
                            y:PoseOffset.orientation.y,
                            z:PoseOffset.orientation.z,
                            w:PoseOffset.orientation.w,
                        },
                    };
                    crate::HmdPoseOffset(&ffipose_offset,position_lock,roation_lock);
                    },
                    ServerRequest::CaptureFrame => unsafe { crate::CaptureFrame() },
                    ServerRequest::InsertIdr => unsafe { crate::RequestIDR() },
                    ServerRequest::ClientCapture => unsafe { crate::ClientCapture()},
                    ServerRequest::NROIQpset( qp_set) => unsafe {
                        if qp_set  {
                            crate::nRoiQpChange(1);
                        } else {
                            crate::nRoiQpChange(-1); 
                        }
                    },                                                                          
                    ServerRequest::HQASizeset( hqrset) => unsafe {
                        if hqrset  { 
                            crate::HQRSizeset(1);
                        } else {
                            crate::HQRSizeset(-1);
                        } 
                    },
                    ServerRequest::RoiQpSet( qp) => unsafe {
                        if qp  {
                            crate::ROIQpChange(1);
                        } else {
                            crate::ROIQpChange(-1);
                        } 
                    },
                    ServerRequest::TestList( list) => unsafe {
                        if list  {
                            crate::TestSequence(1,0);
                        } else {
                            crate::TestSequence(-1,0);
                        } 
                    },
                    ServerRequest::TestNum( num) =>unsafe {
                        if num  {
                            crate::TestSequence(0,1);
                        } else {
                            crate::TestSequence(0,-1);
                        }
                    }
                    ServerRequest::CentreSizeset( center_set) => unsafe {
                        if center_set {
                            crate::CentrSizeset(1);
                        }else {
                            crate::CentrSizeset(-1);
                        }
                    },
                    ServerRequest::COF0set(cof0flag) => unsafe 
                    {   if cof0flag {
                            crate::COF0set(0.5)
                         } else {
                            crate::COF0set(-0.5)
                         }
                    },
                    ServerRequest::COF1set(cof1flag) => unsafe 
                    {   if cof1flag {
                            crate::COF1set(0.01)
                        } else {
                            crate::COF1set(-0.01)
                        } 
                    },
                    ServerRequest::QPDistribution => unsafe {crate::QPDistribution()},
                    ServerRequest::MaxQpSet (addflag) => unsafe {
                        if addflag {
                            crate::MaxQpSet(1)
                        } else {crate::MaxQpSet(-1)}
                    },
                    ServerRequest::GazeVisual=>unsafe {crate::GazeVisual()},
                    ServerRequest::FPSReduce=>unsafe {crate::FPSReduce()},
                    ServerRequest::TDmode=>unsafe {crate::TDmode()},
                    ServerRequest::GaussianBlurStrategy(gbstra)=>  unsafe {                                        
                        if  gbstra == true {
                            crate::UpdateGaussionStrategy(1);
                        }
                        else {
                            crate::UpdateGaussionStrategy(-1)
                        }
                    },
                    ServerRequest::GaussionBlurRoiSize(gbroi) => unsafe {
                        if gbroi ==true {
                            crate::UpdateGaussionRoiSize(0.0061728395*1.0 as f32)
                        }
                        else {
                            crate::UpdateGaussionRoiSize(-0.0061728395*1.0 as f32)
                        }
                    },
                    ServerRequest::GaussionBlurEnble =>unsafe {crate::GaussionEnable()},
                    ServerRequest::SpeedThresholdadd=>unsafe {crate::SpeedThresholdadd()},
                    ServerRequest::SpeedThresholdsub=>unsafe {crate::SpeedThresholdsub()},
                    ServerRequest::StartRecording => unsafe {crate::create_recording_file();crate::RecordGaze()},
                    //ServerRequest::StopRecording => *VIDEO_RECORDING_FILE.lock() = None,
                    ServerRequest::StopRecording =>  unsafe {crate::StopRecordGaze();*VIDEO_RECORDING_FILE.lock() = None; crate::CloseTxtFile() ;crate::RequestIDR()},
                    ServerRequest::FirewallRules(action) => {   
                        if alvr_server_io::firewall_rules(action).is_ok() {
                            info!("Setting firewall rules succeeded!");
                        } else {
                            error!("Setting firewall rules failed!");
                        }
                    }
                    ServerRequest::RegisterAlvrDriver => {
                        alvr_server_io::driver_registration(
                            &[FILESYSTEM_LAYOUT.openvr_driver_root_dir.clone()],
                            true,
                        )
                        .ok();

                        if let Ok(list) = alvr_server_io::get_registered_drivers() {
                            alvr_events::send_event(EventType::DriversList(list));
                        }
                    }
                    ServerRequest::UnregisterDriver(path) => {
                        alvr_server_io::driver_registration(&[path], false).ok();

                        if let Ok(list) = alvr_server_io::get_registered_drivers() {
                            alvr_events::send_event(EventType::DriversList(list));
                        }
                    }
                    ServerRequest::GetDriverList => {
                        if let Ok(list) = alvr_server_io::get_registered_drivers() {
                            alvr_events::send_event(EventType::DriversList(list));
                        }
                    }
                    ServerRequest::RestartSteamvr => crate::notify_restart_driver(),
                    ServerRequest::ShutdownSteamvr => crate::notify_shutdown_driver(),
                }

                reply(StatusCode::OK)?
            } else {
                reply(StatusCode::BAD_REQUEST)?
            }
        }
        "/api/events" => {
            websocket(request, events_sender, |e| {
                protocol::Message::Text(json::to_string(&e).unwrap())
            })
            .await?
        }
        "/api/video-mirror" => {
            let sender = {
                let mut sender_lock = VIDEO_MIRROR_SENDER.lock();
                if let Some(sender) = &mut *sender_lock {
                    sender.clone()
                } else {
                    let (sender, _) = broadcast::channel(WS_BROADCAST_CAPACITY);
                    *sender_lock = Some(sender.clone());

                    sender
                }
            };

            if let Some(config) = &*DECODER_CONFIG.lock() {
                sender.send(config.config_buffer.clone()).ok();
            }

            let res = websocket(request, sender, protocol::Message::Binary).await?;

            unsafe { crate::RequestIDR() };

            res
        }
        "/api/ping" => reply(StatusCode::OK)?,
        other_uri => {
            if other_uri.contains("..") {
                // Attempted tree traversal
                reply(StatusCode::FORBIDDEN)?
            } else {
                let path_branch = match other_uri {
                    "/" => "/index.html",
                    other_path => other_path,
                };

                let maybe_file = tokio::fs::File::open(format!(
                    "{}{path_branch}",
                    FILESYSTEM_LAYOUT.dashboard_dir().to_string_lossy(),
                ))
                .await;

                if let Ok(file) = maybe_file {
                    let mut builder = Response::builder();
                    if other_uri.ends_with(".wasm") {
                        builder = builder.header(CONTENT_TYPE, "application/wasm");
                    }

                    builder
                        .body(Body::wrap_stream(FramedRead::new(file, BytesCodec::new())))
                        .map_err(err!())?
                } else {
                    reply(StatusCode::NOT_FOUND)?
                }
            }
        }
    };

    response.headers_mut().insert(
        CACHE_CONTROL,
        HeaderValue::from_str("no-cache, no-store, must-revalidate").map_err(err!())?,
    );
    response
        .headers_mut()
        .insert(ACCESS_CONTROL_ALLOW_ORIGIN, HeaderValue::from_static("*"));

    Ok(response)
}

pub async fn web_server(events_sender: broadcast::Sender<Event>) -> StrResult {
    let web_server_port = SERVER_DATA_MANAGER
        .read()
        .settings()
        .connection
        .web_server_port;

    let service = service::make_service_fn(|_| {
        let events_sender = events_sender.clone();
        async move {
            StrResult::Ok(service::service_fn(move |request| {
                let events_sender = events_sender.clone();
                async move {
                    let res = http_api(request, events_sender).await;
                    if let Err(e) = &res {
                        alvr_common::show_e(e);
                    }

                    res
                }
            }))
        }
    });

    hyper::Server::bind(&SocketAddr::new(
        "0.0.0.0".parse().unwrap(),
        web_server_port,
    ))
    .serve(service)
    .await
    .map_err(err!())
}
