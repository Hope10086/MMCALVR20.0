use alvr_packets::ServerRequest;
use eframe::egui::Ui;

pub fn debug_tab_ui(ui: &mut Ui) -> Option<ServerRequest> {
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
            request = Some(ServerRequest::QpModeset(true));
        }
        if ui[1].button("All QpSub").clicked() {
            request = Some(ServerRequest::QpModeset(false));
        }
        if ui[2].button("HQA Add").clicked() {
            request = Some(ServerRequest::RoiSizeset(true));
        }
        if ui[3].button("HQA Sub").clicked() {
            request = Some(ServerRequest::RoiSizeset(false));
        }
        if ui[0].button("COF0 set").clicked() {
            request = Some(ServerRequest::COF0set);
        }
        if ui[1].button("COF1 set").clicked() {
            request = Some(ServerRequest::COF1set);
        }
        if ui[2].button("COF0 reset").clicked() {
            request = Some(ServerRequest::COF0reset);
        }
        if ui[3].button("COF1 reset").clicked() {
            request = Some(ServerRequest::COF1reset);
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
            request = Some(ServerRequest::MaxQpSub);
        } 
        if ui[1].button("MaxQp Add").clicked() {   
            request = Some(ServerRequest ::MaxQpAdd);
        }
        if ui[2].button("TD Mode").clicked() {   
            request = Some(ServerRequest ::TDmode);
        }
        if ui[3].button(" Enable Gaussion").clicked() {
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
    });
    

    request
}
