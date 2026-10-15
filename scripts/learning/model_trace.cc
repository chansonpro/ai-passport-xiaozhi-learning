#include <iostream>
#include "learning_model.h"
int main() {
    learning::Model model;
    int button, press;
    while (std::cin >> button >> press) {
        int clip = model.Act(learning::Button(button), learning::Press(press));
        std::cout << int(model.page) << ' ' << model.word << ' ' << model.round << ' '
                  << model.choice << ' ' << model.answered << ' ' << model.question_heard << ' '
                  << clip << '\n';
    }
}
