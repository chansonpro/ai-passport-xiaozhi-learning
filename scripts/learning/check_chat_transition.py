#!/usr/bin/env python3
"""Exercise the production Tick method with fake device states/audio queues."""
from pathlib import Path
import os, subprocess,tempfile
root=Path(__file__).resolve().parents[2];board=root/'main/boards/folotoy/ai-passport'
s=(board/'learning_ui.cc').read_text();start=s.index('void LearningUi::Tick()');end=s.index('void LearningUi::StateTimerCallback',start)
handle=s[s.index('void LearningUi::HandleButton'):s.index('void LearningUi::ReturnToLearning')]
cancel=s[s.index('void LearningUi::CancelAudio()'):s.index('void LearningUi::Speak(')]
hide=s[s.index('void LearningUi::Hide()'):s.index('void LearningUi::Render()')]
code=r'''
#include "learning_model.h"
#include <cassert>
#include <atomic>
#include <mutex>
#include <cstdint>
void lv_screen_load(void*){}
#define ESP_LOGI(...) ((void)0)
enum { kDeviceStateIdle,kDeviceStateStarting,kDeviceStateSpeaking,kDeviceStateListening,kDeviceStateConnecting,kDeviceStateActivating,kAbortReasonNone };
struct Audio { int resets=0; bool IsPlaybackIdle(){return true;} void ResetDecoder(){resets++;} };
struct Application {int state=kDeviceStateIdle,aborts=0,toggles=0,ends=0;Audio audio;static Application& GetInstance(){static Application a;return a;} int GetDeviceState(){return state;}Audio& GetAudioService(){return audio;}void AbortSpeaking(int){aborts++;}void ToggleChatState(){toggles++;}void EndConversation(){ends++;state=kDeviceStateIdle;}};
struct Board {static Board& GetInstance(){static Board b;return b;}Board* GetAudioCodec(){return this;}int output_volume(){return 50;}};
struct DisplayLockGuard {DisplayLockGuard(int*){}operator bool()const{return true;}};
long now_us=100;long esp_timer_get_time(){return now_us;}
struct LearningUi {
 using Page=learning::Page;using Button=learning::Button;using Press=learning::Press;int* display_=nullptr;learning::Model model_;bool chat_mode_=false,voice_control_=false,return_requested_=false,abort_sent_=false,close_sent_=false,active_=false,speaking_=false,dirty_=false;
 int pending_local_clip_=-1,last_volume_=50,last_voice_state_=-1,played=-1,renders=0;long save_after_=1000,return_deadline_=0;bool force_close_sent_=false;void* screen_=nullptr;std::atomic<bool> feeding_{false};
 std::mutex audio_mutex_;uint32_t generation_=0;int pending_clip_=-1;void* chat_screen_=nullptr;
 void Tick();void HandleButton(Button,Press);void CancelAudio();void Show(Page p){model_.page=p;active_=true;chat_mode_=voice_control_=return_requested_=false;return_deadline_=0;force_close_sent_=false;}void Hide();void Render(){renders++;}void Speak(int clip){played=clip;speaking_=true;}void SavePosition(){dirty_=false;}
};
'''+s[start:end]+handle+cancel+hide+r'''
int main(){
 auto& app=Application::GetInstance();
 { LearningUi home;home.active_=true;
   home.HandleButton(learning::Button::kOk,learning::Press::kLongPress);
   assert(home.active_&&home.voice_control_&&!home.chat_mode_&&app.toggles==1);
   app.state=kDeviceStateListening;home.Tick();assert(home.active_);
   home.HandleButton(learning::Button::kOk,learning::Press::kLongPress);
   assert(home.return_requested_&&app.toggles==2);
   app.state=kDeviceStateIdle;home.Tick();assert(home.active_&&!home.voice_control_);
 }
 app=Application{};LearningUi ui;
 ui.chat_mode_=true;ui.Tick();assert(!ui.active_); // idle must not steal chat screen
 ui.return_requested_=true;ui.Tick();assert(ui.active_&&!ui.chat_mode_);
 ui.voice_control_=true;ui.model_.page=learning::Page::kWord;ui.model_.word=2;
 app.state=kDeviceStateSpeaking;ui.Tick();assert(ui.active_); // retain word through TTS
 app.state=kDeviceStateListening;ui.Tick();assert(ui.active_); // retain word through ASR
 ui.pending_local_clip_=2;ui.return_requested_=true;app.state=kDeviceStateSpeaking;
 ui.Tick();ui.Tick();assert(app.aborts==1&&ui.played==-1);
 app.state=kDeviceStateListening;ui.Tick();ui.Tick();assert(app.toggles==1&&ui.played==-1);
 app.state=kDeviceStateIdle;ui.Tick();assert(ui.played==2&&!ui.voice_control_&&ui.model_.word==2&&ui.model_.page==learning::Page::kWord);
 LearningUi wait;wait.chat_mode_=true;wait.return_requested_=true;app.state=kDeviceStateConnecting;wait.Tick();assert(!wait.active_);
 app.state=kDeviceStateIdle;wait.Tick();assert(wait.active_);
 // 播放请求后返回首页或切词必须取消旧请求；切换期间重复请求只保留最新一次。
 for (int action=0;action<3;action++) {
   app=Application{};app.state=kDeviceStateSpeaking;
   LearningUi pending;pending.active_=pending.voice_control_=true;
   pending.model_.page=learning::Page::kWord;
   pending.HandleButton(learning::Button::kOk,learning::Press::kClick);
   assert(pending.pending_local_clip_==0);
   pending.Tick();assert(app.aborts==1);
   if(action==0) pending.HandleButton(learning::Button::kOk,learning::Press::kLongPress);
   if(action==1) pending.HandleButton(learning::Button::kDown,learning::Press::kClick);
   if(action==2) pending.Hide();
   assert(pending.pending_local_clip_==-1);
   app.state=kDeviceStateIdle;pending.Tick();assert(pending.played==-1);
 }
 app=Application{};app.state=kDeviceStateSpeaking;
 LearningUi repeated;repeated.active_=repeated.voice_control_=true;repeated.model_.page=learning::Page::kWord;
 repeated.HandleButton(learning::Button::kOk,learning::Press::kClick);repeated.Tick();
 repeated.HandleButton(learning::Button::kOk,learning::Press::kClick);repeated.Tick();
 assert(app.aborts==1);
 repeated.HandleButton(learning::Button::kDown,learning::Press::kClick);
 repeated.HandleButton(learning::Button::kOk,learning::Press::kClick);repeated.Tick();
 assert(app.aborts==1&&repeated.pending_local_clip_==1);
 app.state=kDeviceStateListening;repeated.Tick();
 repeated.HandleButton(learning::Button::kOk,learning::Press::kClick);repeated.Tick();
 assert(app.toggles==1);
 app.state=kDeviceStateIdle;repeated.Tick();assert(repeated.played==1);
 repeated.played=-1;repeated.Tick();assert(repeated.played==-1);
 // 首页停止对话也不得留下排队音频。
 app.state=kDeviceStateSpeaking;LearningUi home;home.active_=home.voice_control_=true;
 home.pending_local_clip_=0;home.HandleButton(learning::Button::kOk,learning::Press::kLongPress);
 assert(home.pending_local_clip_==-1);

 // Missing tts.stop must not trap the UI or lose the requested local clip.
 app=Application{};app.state=kDeviceStateSpeaking;
 LearningUi timeout;timeout.active_=timeout.voice_control_=timeout.return_requested_=true;
 timeout.model_.page=learning::Page::kWord;timeout.pending_local_clip_=3;
 timeout.Tick();assert(app.aborts==1&&app.ends==0);
 now_us+=1999999;timeout.Tick();assert(app.ends==0);
 now_us+=1;timeout.Tick();assert(app.ends==1);
 timeout.Tick();assert(timeout.played==3&&!timeout.return_requested_);
 timeout.Tick();assert(app.ends==1);
 // Connecting can also be cancelled when a handshake never finishes.
 app=Application{};app.state=kDeviceStateConnecting;
 LearningUi connecting;connecting.return_requested_=true;
 connecting.Tick();now_us+=2000000;connecting.Tick();connecting.Tick();
 assert(app.ends==1&&connecting.active_&&!connecting.return_requested_);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cc').write_text(code)
 subprocess.run([os.environ.get('CXX', 'c++'),'-std=c++17','-I',str(board),str(p/'test.cc'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: production Tick preserves chat and voice-learning screens; aborts/closes once before local playback; handles connection failure return.')
