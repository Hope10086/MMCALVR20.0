use alvr_common::{glam::{Quat, Vec3}, log::info, Pose};
use alvr_packets::ServerRequest;
use eframe::{egui::{Slider, Ui}, emath::Numeric};

pub fn debug_tab_ui(ui: &mut Ui, 
    position_offset: &mut Vec3, 
    rotation_offset: &mut Vec3 ,
    _position_lock :&mut bool,
    _roation_lock :&mut bool,
    _pose_offset_enable :&mut bool,
    _eyemovementset_enable :&mut bool,
    eyemovement_speed: &mut Vec3,
) 
    -> Option<ServerRequest> {
    let mut request = None;

    ui.columns(4, |ui| {
        if ui[0].button("Capture frame").clicked() {
            request = Some(ServerRequest::CaptureFrame);
        }

        if ui[1].button("Insert IDR").clicked() {
            request = Some(ServerRequest::InsertIdr);
        }

        if ui[2].button("Start recording").clicked() {
            request = Some(ServerRequest::StartRecording);
        }

        if ui[3].button("Stop recording").clicked() {
            request = Some(ServerRequest::StopRecording);
        }
        if ui[0].button("All QpAdd").clicked() {
            request = Some(ServerRequest::AllQpAdd(true));
        }
        if ui[1].button("All QpSub").clicked() {
            request = Some(ServerRequest::AllQpAdd(false));
        }
        if ui[2].button("HQA Add").clicked() {
            request = Some(ServerRequest::RoiSizeset(true));
        }
        if ui[3].button("HQA Sub").clicked() {
            request = Some(ServerRequest::RoiSizeset(false));
        }
        if ui[0].button("COF0 Add").clicked() {
            request = Some(ServerRequest::COF0set(true));
        }
        if ui[1].button("COF1 Add").clicked() {
            request = Some(ServerRequest::COF1set(true));
        }
        if ui[2].button("COF0 Sub").clicked() {
            request = Some(ServerRequest::COF0set(false));
        }
        if ui[3].button("COF1 Sub").clicked() {
            request = Some(ServerRequest::COF1set(false));
        }
        
        if ui[0].button("QP Mode").clicked() {
            request = Some(ServerRequest::QPDistribution);
        }
        if ui[1].button("ROISize Add").clicked() {
            request = Some(ServerRequest::CentreSizeset(true));
        }
        if ui[2].button("ROISize Sub").clicked() {
            request =  Some(ServerRequest::CentreSizeset(false));
        }
        if ui[3].button("GazeVisual").clicked() {
            request = Some(ServerRequest::GazeVisual);
            
        }
        if ui[0].button("MaxQP Sub").clicked(){
            request = Some(ServerRequest::MaxQpSet(false));
        } 
        if ui[1].button("MaxQp Add").clicked() {   
            request = Some(ServerRequest ::MaxQpSet(true));
        }
        if ui[2].button("TD Mode").clicked() {   
            request = Some(ServerRequest ::TDmode);
        }
        if ui[3].button("Enable Gaussion").clicked() {
            request = Some(ServerRequest::GaussionBlurEnble);
        }

        if ui[0].button("Speed Threshold add").clicked() {   
            request = Some(ServerRequest ::SpeedThresholdadd);
        }
        if ui[1].button("Speed Threshold sub").clicked() {   
            request = Some(ServerRequest ::SpeedThresholdsub);
        }
        if ui[2].button("StrategyAdd").clicked() {
            request = Some(ServerRequest ::GaussianBlurStrategy(true))
            
        }
        if ui[3].button("StrategySub").clicked() {
            request = Some(ServerRequest ::GaussianBlurStrategy(false))
            
        }
        if ui[0].button("GaussianSizeAdd").clicked() {   
            request = Some(ServerRequest ::GaussionBlurRoiSize(true));
        }
        if ui[1].button("GaussianSizeSub").clicked() {   
            request = Some(ServerRequest ::GaussionBlurRoiSize(false));
        }
        if ui[2].button("Client Capture").clicked() {
            request = Some(ServerRequest::ClientCapture)
        }
        if ui[3].button("FPS Reudce").clicked() {
            request = Some(ServerRequest::FPSReduce)
        } 
        if ui[0].button("Roi Qp StrategyAdd").clicked() {
            request = Some(ServerRequest::RoiQpSet(true))
        } 
        if ui[1].button("Roi Qp StrategySub").clicked() {
            request = Some(ServerRequest::RoiQpSet(false))
        } 
        if ui[2].button("Test List Add").clicked() {
            request = Some(ServerRequest::TestList(true))
        }
        if ui[3].button("Test List Sub").clicked() {
            request = Some(ServerRequest::TestList(false))
        }

        if ui[0].button("Test Number Add").clicked() {
            request = Some(ServerRequest::TestNum(true))
        } 
        if ui[1].button("Test Number Sub").clicked() {
            request = Some(ServerRequest::TestNum(false))
        }

        if ui[0].button("EyeMoveModeSet").clicked() {
            *_eyemovementset_enable =! *_eyemovementset_enable;
        } 
        ui[1].add(Slider::new(&mut eyemovement_speed.x, 0.0..=  1024.0).text("Eye Move Speed (°/s)"));
        if *_eyemovementset_enable {
            request = Some(ServerRequest::EyeMoveModeSet(eyemovement_speed.x.clone()))
        }
        if ui[0].button("position lock").clicked() {
            *_position_lock = !*_position_lock;
        }
        if ui[0].button("rotation lock").clicked() {
            *_roation_lock = !*_roation_lock;
        }
        //let mut position_offset= Vec3::new(0.0,0.0,0.0);
        ui[0].label("Hmd's Pose Offset:");
        ui[0].label("----------------------------------");
        if ui[0].button("Enable Offset Set").clicked() {
            *_pose_offset_enable = true;
        }
        ui[0].add(Slider::new(&mut position_offset.x, -10.0..=10.0).text("Translate X:(m)"));
        ui[0].add(Slider::new(&mut position_offset.y, -10.0..=10.0).text("Translate Y:(m)"));
        ui[0].add(Slider::new(&mut position_offset.z, -10.0..=10.0).text("Translate Z:(m)"));

       // let mut rotation_offset =Vec3::new(0.0,0.0,0.0);
        ui[0].add(Slider::new(&mut rotation_offset.x, -180.0..=180.0).text("Rotation X:(deg)"));
        ui[0].add(Slider::new(&mut rotation_offset.y, -180.0..=180.0).text("Rotation Y:(deg)"));
        ui[0].add(Slider::new(&mut rotation_offset.z, -180.0..=180.0).text("Rotation Z:(deg)"));
        let deg_to_pi = std::f32::consts::PI / 180.0;
        let quat_fromx = Quat::from_rotation_x(rotation_offset.x * deg_to_pi);
        let quat_fromy = Quat::from_rotation_y(rotation_offset.y * deg_to_pi);
        let quat_fromz = Quat::from_rotation_z(rotation_offset.z * deg_to_pi);

        let quat_offset =quat_fromx*quat_fromy*quat_fromz ;
        let pose_offset = Pose {
            orientation: quat_offset,
            position: position_offset.clone(),
        };
        if *_pose_offset_enable {
            request = Some(ServerRequest::HmdPoseOffset(pose_offset,*_position_lock,*_roation_lock));
            *_pose_offset_enable = false;
        }


              
    });
    

    request
}
