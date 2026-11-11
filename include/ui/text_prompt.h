#ifndef TEXT_PROMPT_H
#define TEXT_PROMPT_H

#include <string>

namespace UI {

// Opens the system keyboard. Returns false when the user cancels; the text may be empty (it is allowed).
bool promptText(const std::string &hint, const std::string &initial, std::string &out, int maxLength,
                bool multiline = false);

} // namespace UI

#endif // TEXT_PROMPT_H
