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
    });

    ui.columns(4, |ui| {
        if ui[0].button("QpMode set").clicked() {
            request = Some(ServerRequest::QpModeset);
        }
        if ui[1].button("RoiSize set").clicked() {
            request = Some(ServerRequest::RoiSizeset);
        }
        if ui[2].button("QpMode zero").clicked() {
            request = Some(ServerRequest::QpModezero);
        }
        if ui[3].button("RoiSize zero").clicked() {
            request = Some(ServerRequest::RoiSizezero);
        }
    });
    
    ui.columns(4, |ui| {
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
    });

    ui.columns(3, |ui| {
        if ui[0].button("QP Distribution method").clicked() {
            request = Some(ServerRequest::QPDistribution);
        }
        if ui[1].button("CentreSize set").clicked() {
            request = Some(ServerRequest::CentreSizeset);
        }
        if ui[2].button("CentreSize reset").clicked() {
            request = Some(ServerRequest::CentreSizereset);
        }
    });
    

    


    request
}
