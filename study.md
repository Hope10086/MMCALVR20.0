
1. ALVR传输时采用的是音视频的裸流数据，没有进行封装。
        res = spawn_cancelable(game_audio_loop) => res,
        res = spawn_cancelable(microphone_loop) => res,
        res = spawn_cancelable(tracking_send_loop) => res,
        res = spawn_cancelable(statistics_send_loop) => res,
        res = spawn_cancelable(video_receive_loop) => res,
        res = spawn_cancelable(haptics_receive_loop) => res,
        res = spawn_cancelable(control_send_loop) => res,
        res = spawn_cancelable(gaussion_receive_loop) => res,
