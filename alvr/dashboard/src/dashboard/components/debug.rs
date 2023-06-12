use alvr_packets::ServerRequest;
use eframe::egui::Ui;

pub fn debug_tab_ui(ui: &mut Ui) -> Option<ServerRequest> {
    let mut request = None;

    ui.columns(8, |ui| {
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
        if ui[4].button("QpMode set").clicked() {
            request = Some(ServerRequest::QpModeset);
        }
        if ui[5].button("RoiSize set").clicked() {
            request = Some(ServerRequest::RoiSizeset);
        }
        if ui[6].button("QpMode zero").clicked() {
            request = Some(ServerRequest::QpModezero);
        }
        if ui[7].button("RoiSize zero").clicked() {
            request = Some(ServerRequest::RoiSizezero);
        }
    });

    request
}
