#!/usr/bin/env python3
"""Exercise the production Tick method with fake device states/audio queues."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];board=root/'main/boards/folotoy/ai-passport'
s=(board/'learning_ui.cc').read_text();start=s.index('void LearningUi::Tick()');end=s.index('void LearningUi::StateTimerCallback',start)
handle=s[s.index('void LearningUi::HandleButton'):s.index('void LearningUi::ReturnToLearning')]
code=r'''
#include "learning_model.h"
#include <cassert>
#include <atomic>
#define ESP_LOGI(...) ((void)0)
enum { kDeviceStateIdle,kDeviceStateStarting,kDeviceStateSpeaking,kDeviceStateListening,kDeviceStateConnecting,kDeviceStateActivating,kAbortReasonNone };
struct Audio { int resets=0; bool IsPlaybackIdle(){return true;} void ResetDecoder(){resets++;} };
struct Application {int state=kDeviceStateIdle,aborts=0,toggles=0;Audio audio;static Application& GetInstance(){static Application a;return a;} int GetDeviceState(){return state;}Audio& GetAudioService(){return audio;}void AbortSpeaking(int){aborts++;}void ToggleChatState(){toggles++;}};
struct Board {static Board& GetInstance(){static Board b;return b;}Board* GetAudioCodec(){return this;}int output_volume(){return 50;}};
struct DisplayLockGuard {DisplayLockGuard(int*){}operator bool()const{return true;}};
long esp_timer_get_time(){return 100;}
struct LearningUi {
 using Page=learning::Page;using Button=learning::Button;using Press=learning::Press;int* display_=nullptr;learning::Model model_;bool chat_mode_=false,voice_control_=false,return_requested_=false,abort_sent_=false,close_sent_=false,active_=false,speaking_=false,dirty_=false;
 int pending_local_clip_=-1,last_volume_=50,last_voice_state_=-1,played=-1,renders=0;long save_after_=1000;void* screen_=nullptr;std::atomic<bool> feeding_{false};
 void Tick();void HandleButton(Button,Press);void CancelAudio(){speaking_=false;}void Show(Page p){model_.page=p;active_=true;chat_mode_=voice_control_=return_requested_=false;}void Hide(){active_=false;}void Render(){renders++;}void Speak(int clip){played=clip;speaking_=true;}void SavePosition(){dirty_=false;}
};
'''+s[start:end]+handle+r'''
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
 ui.voice_control_=true;ui.model_.page=learning::Page::kPoem;ui.model_.line=2;
 app.state=kDeviceStateSpeaking;ui.Tick();assert(ui.active_); // retain poem through TTS
 app.state=kDeviceStateListening;ui.Tick();assert(ui.active_); // retain poem through ASR
 ui.pending_local_clip_=10;ui.return_requested_=true;app.state=kDeviceStateSpeaking;
 ui.Tick();ui.Tick();assert(app.aborts==1&&ui.played==-1);
 app.state=kDeviceStateListening;ui.Tick();ui.Tick();assert(app.toggles==1&&ui.played==-1);
 app.state=kDeviceStateIdle;ui.Tick();assert(ui.played==10&&!ui.voice_control_&&ui.model_.line==2&&ui.model_.page==learning::Page::kPoem);
 LearningUi wait;wait.chat_mode_=true;wait.return_requested_=true;app.state=kDeviceStateConnecting;wait.Tick();assert(!wait.active_);
 app.state=kDeviceStateIdle;wait.Tick();assert(wait.active_);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cc').write_text(code)
 subprocess.run(['clang++','-std=c++17','-I',str(board),str(p/'test.cc'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: production Tick preserves chat and voice-learning screens; aborts/closes once before local playback; handles connection failure return.')
