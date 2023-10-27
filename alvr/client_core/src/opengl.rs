#![allow(unused_variables)]

use alvr_common::{glam::{UVec2, Quat, Vec3, bool, Vec2}, 
                  Fov, Pose, prelude::info};
use alvr_packets::FaceData;
use alvr_session::FoveatedRenderingDesc;
use glyph_brush_layout::{
    ab_glyph::{Font, FontRef, ScaleFont},
    FontId, GlyphPositioner, HorizontalAlign, Layout, SectionGeometry, SectionText, VerticalAlign,
};

use crate::c_api::{alvr_log};

use std::{
    time::{Duration, Instant}, ffi::{c_char, c_void, CStr, CString}, env::consts
};

#[cfg(target_os = "android")]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

const HUD_TEXTURE_WIDTH: usize = 1280;
const HUD_TEXTURE_HEIGHT: usize = 720;
const FONT_SIZE: f32 = 50_f32;

#[no_mangle]
pub unsafe extern "C" fn log_info_message(message: *const c_char) {
    let message = CStr::from_ptr(message).to_str().unwrap();
    info!("[ALVR GPU:]{message}");
}


pub struct RenderViewInput {
    pub pose: Pose,
    pub fov: Fov,
    pub swapchain_index: u32,
}

pub fn initialize() {
    #[cfg(target_os = "android")]
    unsafe {
        pub static LOBBY_ROOM_GLTF: &[u8] = include_bytes!("../resources/loading.gltf");
        pub static LOBBY_ROOM_BIN: &[u8] = include_bytes!("../resources/buffer.bin");

        LOBBY_ROOM_GLTF_PTR = LOBBY_ROOM_GLTF.as_ptr();
        LOBBY_ROOM_GLTF_LEN = LOBBY_ROOM_GLTF.len() as _;
        LOBBY_ROOM_BIN_PTR = LOBBY_ROOM_BIN.as_ptr();
        LOBBY_ROOM_BIN_LEN = LOBBY_ROOM_BIN.len() as _;

        initGraphicsNative();
    }
}

pub fn destroy() {
    #[cfg(target_os = "android")]
    unsafe {
        destroyGraphicsNative();
    }
}

pub fn resume(preferred_view_resolution: UVec2, swapchain_textures: [Vec<u32>; 2]) {
    #[cfg(target_os = "android")]
    unsafe {
        let swapchain_length = swapchain_textures[0].len();
        let mut swapchain_textures = [
            swapchain_textures[0].as_ptr(),
            swapchain_textures[1].as_ptr(),
        ];

        prepareLobbyRoom(
            preferred_view_resolution.x as _,
            preferred_view_resolution.y as _,
            swapchain_textures.as_mut_ptr(),
            swapchain_length as _,
        );
    }
}

pub fn pause() {
    #[cfg(target_os = "android")]
    unsafe {
        destroyRenderers();
    }
}

pub fn start_stream(
    view_resolution: UVec2,
    swapchain_textures: [Vec<u32>; 2],
    foveated_rendering: Option<FoveatedRenderingDesc>,
) {
    #[cfg(target_os = "android")]
    unsafe {
        let config = FfiStreamConfig {
            viewWidth: view_resolution.x,
            viewHeight: view_resolution.y,
            swapchainTextures: [
                swapchain_textures[0].as_ptr(),
                swapchain_textures[1].as_ptr(),
            ],
            swapchainLength: swapchain_textures[0].len() as _,
            enableFoveation: foveated_rendering.is_some().into(),
            foveationCenterSizeX: foveated_rendering
                .as_ref()
                .map(|f| f.center_size_x)
                .unwrap_or_default(),
            foveationCenterSizeY: foveated_rendering
                .as_ref()
                .map(|f| f.center_size_y)
                .unwrap_or_default(),
            foveationCenterShiftX: foveated_rendering
                .as_ref()
                .map(|f| f.center_shift_x)
                .unwrap_or_default(),
            foveationCenterShiftY: foveated_rendering
                .as_ref()
                .map(|f| f.center_shift_y)
                .unwrap_or_default(),
            foveationEdgeRatioX: foveated_rendering
                .as_ref()
                .map(|f| f.edge_ratio_x)
                .unwrap_or_default(),
            foveationEdgeRatioY: foveated_rendering
                .as_ref()
                .map(|f| f.edge_ratio_y)
                .unwrap_or_default(),
        };

        streamStartNative(config);
    }
}

pub fn update_hud_message(message: &str) {
    let ubuntu_font =
        FontRef::try_from_slice(include_bytes!("../resources/Ubuntu-Medium.ttf")).unwrap();

    let section_glyphs = Layout::default()
        .h_align(HorizontalAlign::Center)
        .v_align(VerticalAlign::Center)
        .calculate_glyphs(
            &[&ubuntu_font],
            &SectionGeometry {
                screen_position: (
                    HUD_TEXTURE_WIDTH as f32 / 2_f32,
                    HUD_TEXTURE_HEIGHT as f32 / 2_f32,
                ),
                ..Default::default()
            },
            &[SectionText {
                text: message,
                scale: FONT_SIZE.into(),
                font_id: FontId(0),
            }],
        );

    let scaled_font = ubuntu_font.as_scaled(FONT_SIZE);

    let mut buffer = vec![0_u8; HUD_TEXTURE_WIDTH * HUD_TEXTURE_HEIGHT * 4];

    for section_glyph in section_glyphs {
        if let Some(outlined) = scaled_font.outline_glyph(section_glyph.glyph) {
            let bounds = outlined.px_bounds();
            outlined.draw(|x, y, alpha| {
                let x = x as usize + bounds.min.x as usize;
                let y = y as usize + bounds.min.y as usize;
                buffer[(y * HUD_TEXTURE_WIDTH + x) * 4 + 3] = (alpha * 255.0) as u8;
            });
        }
    }

    #[cfg(target_os = "android")]
    unsafe {
        updateLobbyHudTexture(buffer.as_ptr());
    }
}

pub fn render_lobby(view_inputs: [RenderViewInput; 2]) {
    #[cfg(target_os = "android")]
    unsafe {
        let eye_inputs = [
            FfiViewInput {
                position: view_inputs[0].pose.position.to_array(),
                orientation: view_inputs[0].pose.orientation.to_array(),
                fovLeft: view_inputs[0].fov.left,
                fovRight: view_inputs[0].fov.right,
                fovUp: view_inputs[0].fov.up,
                fovDown: view_inputs[0].fov.down,
                swapchainIndex: view_inputs[0].swapchain_index as _,
            },
            FfiViewInput {
                position: view_inputs[1].pose.position.to_array(),
                orientation: view_inputs[1].pose.orientation.to_array(),
                fovLeft: view_inputs[1].fov.left,
                fovRight: view_inputs[1].fov.right,
                fovUp: view_inputs[1].fov.up,
                fovDown: view_inputs[1].fov.down,
                swapchainIndex: view_inputs[1].swapchain_index as _,
            },
        ];

        renderLobbyNative(eye_inputs.as_ptr());
    }
}

pub fn render_stream(hardware_buffer: *mut std::ffi::c_void, swapchain_indices: [u32; 2]) {
    #[cfg(target_os = "android")]
    unsafe {
        InfoLog =Some(log_info_message);

    //   let render_time_beagin = Instant::now();                                           
        renderStreamNative(hardware_buffer, swapchain_indices.as_ptr());

      
    //    let mut InfoString = CString::new("render time:")
    //                     .expect("InfoString CString Failed ");
    //    let rendercost = (Instant::now() - render_time_beagin)
    //                                .as_micros()
    //                                .to_string();
    //    let rendercost_cstr = CString::new(rendercost).expect("CString conversion failed") ;
    //    let rendercost_cstr_u8 = rendercost_cstr
    //                            .as_ptr()
    //                             as *const u8;
    //    //alvr_log(crate::c_api::AlvrLogLevel::Info, InfoString.as_ptr() as *const u8);
    //    alvr_log(crate::c_api::AlvrLogLevel::Info, rendercost_cstr_u8);
    }
}


pub fn update_gaussion_message (flag :bool ,strategynum : i32 ,roisize:f32)
{
   #[cfg(target_os = "android")]
    unsafe {
        updategussionflg(flag ,  strategynum,roisize);
        let  strategy  = strategynum.to_string();
        let  roiradius = roisize.to_string();
        let gaussionset = CString::new(format!("(Weight Strategy:{}, ROI radius:{})", strategy, roiradius)).expect("gaussionset failed");

        alvr_log(crate::c_api::AlvrLogLevel::Info,
            gaussionset.as_ptr() );
    }
}
pub fn to_local_eyes(
    raw_global_head: Pose,
    raw_global_eyes: [Option<Pose>; 2],
) -> [Option<Pose>; 2] {
    [
        raw_global_eyes[0].map(|e| raw_global_head.inverse() * e),
        raw_global_eyes[1].map(|e| raw_global_head.inverse() * e),
    ]
}

pub fn quat_conjugate(quat: &Quat) 
-> Quat {
         Quat::from_xyzw(-1.0*quat.x, -1.0*quat.y, -1.0*quat.z, quat.w)
}

pub fn quat_rotate_vector(
    quat:&Quat,
    vector:&Vec3,
) ->Vec3 {
     let pin = Quat::from_xyzw(vector.x, vector.y, vector.z, 0.0);
     let conjugate =quat_conjugate(quat);
     let pout = (quat.mul_quat(pin)).mul_quat(conjugate);
    
    Vec3 { x: (pout.x), y: (pout.y), z: (pout.z) }
    
}




pub fn get_gaze_center( leftfpv:Fov, eyegaze: [Option<Pose>; 2],
) ->[Option<Vec2> ;2]{
         if let[Some(pose1),Some(pose2)]  = eyegaze {
            // let contfov = Fov{
            //     left:-0.942478,
            //     right:0.698132,
            //     up: 0.733038,
            //     down:-0.942478};
            let Zaix = Vec3{x:0.0,y:0.0,z:-1.0};
            let leftvec = quat_rotate_vector(&pose1.orientation, &Zaix);    
            let rightvec = quat_rotate_vector(&pose2.orientation, &Zaix);    
           
            let center1 = Some(Vec2 {
                x:((-1.0*leftfpv.left.tan()+ (-1.0 * leftvec.x/leftvec.z) ))/(-1.0*leftfpv.left.tan()+leftfpv.right.tan()),
                y:((     leftfpv.up.tan()+ (  1.0 * leftvec.y/leftvec.z) ))/(-1.0*leftfpv.down.tan()+leftfpv.up.tan()), 
            });
            let center2 = Some(Vec2 {
                x:((     leftfpv.right.tan() +(-1.0 * rightvec.x/rightvec.z)) )/(-1.0*leftfpv.left.tan()+leftfpv.right.tan()),
                y:((     leftfpv.up.tan()  +( 1.0 * rightvec.y/rightvec.z)) )/(-1.0*leftfpv.down.tan()+leftfpv.up.tan()), 
            });
            [center1, center2] 
         }else {
            let defaultcenter = Vec2 { x: 0.5, y: 0.5 };
            [Some(defaultcenter),Some(defaultcenter)]
         }
}

pub fn calculate_gazecenter( target_timestamp :Duration, rawfacedata: FaceData ,rawheadpose: Pose, rawfov: Fov) {
    
    #[cfg(target_os = "android")]
    unsafe{
        let local_eye_gazes = to_local_eyes(
            rawheadpose,
            rawfacedata.eye_gazes);
        let local_gazecenter = get_gaze_center(rawfov, local_eye_gazes);
        updategazecenter(
            target_timestamp.as_secs(),
            local_gazecenter[0].unwrap().x * 0.5,
            local_gazecenter[0].unwrap().y,
            local_gazecenter[1].unwrap().x *0.5 + 0.5,
            local_gazecenter[1].unwrap().y
        );
        // let lxstr = (local_gazecenter[0].unwrap().x * 0.5).to_string();
        // let lystr = local_gazecenter[0].unwrap().y.to_string();
        // let lstr = format!("({}, {})", lxstr, lystr);

        //    let fovlog =rawfov.down.to_string();

        // alvr_log(crate::c_api::AlvrLogLevel::Info,
        //     CString::new(fovlog).expect("msgerro")
        //                                .as_ptr() 
        //         );
    }
}