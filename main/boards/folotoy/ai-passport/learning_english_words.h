#ifndef LEARNING_ENGLISH_WORDS_H
#define LEARNING_ENGLISH_WORDS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 幼儿英语词库：6 个主题 × 10 个单词，适合 3-6 岁儿童启蒙。
// 参考社区玩法「宝宝英语 · 幼儿单词卡」(#1189) 的主题分类自建。

typedef struct {
    const char* word;       // 英文单词
    const char* phonetic;   // 音标（英式）
    const char* zh;         // 中文释义
    const char* theme;      // 主题（中文）
} english_word_t;

extern const english_word_t g_english_words[];
extern const size_t g_english_word_count;

#ifdef __cplusplus
}
#endif

#endif  // LEARNING_ENGLISH_WORDS_H
