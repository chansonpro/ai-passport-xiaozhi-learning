#pragma once

// Deterministic, platform-independent counterpart of prototypes/learning-v1.
namespace learning {
enum class Page { kNone, kHome, kWord, kQuiz, kPoem, kRecall };
enum class Button { kUp, kDown, kOk };
enum class Press { kClick, kLongPress, kDoubleClick };
inline constexpr const char* kWords[] = {"apple", "banana", "cat", "dog"};
inline constexpr const char* kChinese[] = {"苹果", "香蕉", "小猫", "小狗"};
inline constexpr const char* kLines[] = {"床前明月光", "疑是地上霜", "举头望明月", "低头思故乡"};
struct Model {
    Page page = Page::kHome;
    int word = 0, line = 0, selected = 0, round = 0, choice = 0;
    bool answered = false, question_heard = false, recall_mode = false;
    const char* message = "";
    // -1 is silence; 0..3 words, 4..7 questions, 8..11 poem lines.
    int Act(Button button, Press press) {
        if (press == Press::kDoubleClick)
            return -1;
        if (press == Press::kLongPress) {
            if (button == Button::kOk && page != Page::kHome) {
                page = page == Page::kQuiz ? Page::kWord : Page::kHome;
                selected = 0;
                message = "";
            }
            return -1;
        }
        message = "";
        if (page == Page::kHome) {
            if (button == Button::kOk) {
                page = selected == 0 ? Page::kWord : (recall_mode ? Page::kRecall : Page::kPoem);
                selected = 0;
            } else
                selected = 1 - selected;
        } else if (page == Page::kWord) {
            if (button == Button::kOk)
                return word;
            if (button == Button::kDown && word == 3) {
                page = Page::kQuiz;
                round = choice = 0;
                answered = false;
                question_heard = true;
                return 4;
            }
            word = (word + (button == Button::kUp ? 3 : 1)) % 4;
        } else if (page == Page::kQuiz) {
            if (button != Button::kOk) {
                if (!answered)
                    choice = 1 - choice;
                message = answered ? "找到了！中间键继续" : "中间键确认";
            } else if (answered) {
                if (++round == 4) {
                    page = Page::kWord;
                    word = 0;
                    message = "今天找到了四个词！";
                } else {
                    choice = 0;
                    answered = false;
                    question_heard = true;
                    return 4 + round;
                }
            } else if (!question_heard) {
                question_heard = true;
                return 4 + round;
            } else {
                answered = choice == round % 2;
                message = answered ? "找到了！中间键继续" : "再试一次，中间确认";
                if (!answered)
                    return 4 + round;
            }
        } else if (page == Page::kPoem || page == Page::kRecall) {
            if (button == Button::kOk) {
                if (selected == 1) {
                    recall_mode = !recall_mode;
                    page = recall_mode ? Page::kRecall : Page::kPoem;
                    line = selected = 0;
                } else
                    return 8 + line;
            } else if (button == Button::kDown && line == 3 && selected == 0)
                selected = 1;
            else if (button == Button::kUp && selected == 1)
                selected = 0;
            else {
                selected = 0;
                line = (line + (button == Button::kUp ? 3 : 1)) % 4;
            }
        }
        return -1;
    }
    int Option(int slot) const { return slot == round % 2 ? round : (round + 1) % 4; }
};
}  // namespace learning
