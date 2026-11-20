#pragma once

#include <QDialog>
#include <vector>
#include <rules.h>

class QPlainTextEdit;
class QCheckBox;
class QComboBox;

// add / edit dialog for process rules
// the executable can be typed (or pasted) directly, "Browse..." is optional
class RuleDialog : public QDialog {
public:
    // existing != nullptr edits that rule, otherwise new rules are created (one per line)
    RuleDialog(QWidget* parent, const std::vector<int>& availablePStates, const ProcessRule* existing = nullptr);

    // valid after exec() returned QDialog::Accepted
    const std::vector<ProcessRule>& rules() const;

    void accept() override;

private:
    QPlainTextEdit* input;
    QCheckBox* matchPath;
    QComboBox* pState;
    bool isEditing;
    std::vector<ProcessRule> result;
};
