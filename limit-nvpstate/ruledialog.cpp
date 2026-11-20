#include <ruledialog.h>
#include <utils.h>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>

RuleDialog::RuleDialog(QWidget* parent, const std::vector<int>& availablePStates, const ProcessRule* existing)
    : QDialog(parent), isEditing(existing != nullptr) {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(isEditing ? "Edit Process" : "Add Process");
    setMinimumWidth(480);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // executable name / path
    layout->addWidget(new QLabel(isEditing ? "Executable name or absolute path:" : "Executable name or absolute path (one per line):"));

    input = new QPlainTextEdit(this);
    input->setPlaceholderText("game.exe\nC:\\Games\\game\\game.exe");
    input->setLineWrapMode(QPlainTextEdit::NoWrap);
    input->setFixedHeight(isEditing ? 40 : 90);
    layout->addWidget(input);

    QPushButton* browse = new QPushButton("Browse...", this);
    QHBoxLayout* browseLayout = new QHBoxLayout();
    browseLayout->addWidget(browse);
    browseLayout->addStretch();
    layout->addLayout(browseLayout);

    connect(browse, &QPushButton::clicked, [this]() {
        QStringList files;

        if (isEditing) {
            QString file = QFileDialog::getOpenFileName(this, "Select executable", "", tr("Executable Files (*.exe);;All Files (*.*)"));

            if (!file.isEmpty()) {
                files << file;
            }
        } else {
            files = QFileDialog::getOpenFileNames(this, "Select executables", "", tr("Executable Files (*.exe);;All Files (*.*)"));
        }

        if (files.isEmpty()) {
            return;
        }

        for (QString& file : files) {
            file = QDir::toNativeSeparators(file);
        }

        if (isEditing) {
            input->setPlainText(files.first());
        } else {
            QString current = input->toPlainText().trimmed();
            input->setPlainText((current.isEmpty() ? "" : current + "\n") + files.join("\n"));
        }
        });

    // match mode
    matchPath = new QCheckBox("Match absolute path (otherwise only the process name is compared)", this);
    layout->addWidget(matchPath);

    // P-State
    pState = new QComboBox(this);
    pState->addItem("P0 (unlimited)", 0);

    for (int available : availablePStates) {
        if (available != 0) {
            pState->addItem(QString("P%1").arg(available), available);
        }
    }

    QHBoxLayout* pStateLayout = new QHBoxLayout();
    pStateLayout->addWidget(new QLabel("P-State while running / in foreground:"));
    pStateLayout->addWidget(pState, 1);
    layout->addLayout(pStateLayout);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (existing) {
        input->setPlainText(QString::fromStdString(existing->pattern));
        matchPath->setChecked(existing->matchPath);

        int index = pState->findData(existing->pstate);

        if (index < 0) {
            // keep rules whose P-State isn't listed for this GPU editable
            pState->addItem(QString("P%1").arg(existing->pstate), existing->pstate);
            index = pState->count() - 1;
        }

        pState->setCurrentIndex(index);
    }
}

const std::vector<ProcessRule>& RuleDialog::rules() const {
    return result;
}

void RuleDialog::accept() {
    const bool isMatchPath = matchPath->isChecked();
    const int selectedPState = pState->currentData().toInt();

    std::string ownPath = getProgramPath();
    const std::string ownName = toLower(getBaseName(ownPath));

    std::vector<ProcessRule> parsed;
    QStringList errors;

    for (const QString& line : input->toPlainText().split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        std::string pattern, error;

        if (!normalizeRulePattern(line.toStdString(), isMatchPath, pattern, error)) {
            errors << QString::fromStdString(error);
            continue;
        }

        // the program itself can't be an exception
        bool isSelf = isMatchPath ? toLower(pattern) == toLower(ownPath) : toLower(pattern) == ownName;

        if (isSelf) {
            errors << QString::fromStdString("\"" + pattern + "\" is this program and can't be added");
            continue;
        }

        // ignore duplicate lines
        bool isDuplicate = false;

        for (const ProcessRule& rule : parsed) {
            if (toLower(rule.pattern) == toLower(pattern)) {
                isDuplicate = true;
                break;
            }
        }

        if (isDuplicate) {
            continue;
        }

        ProcessRule rule;
        rule.pattern = pattern;
        rule.matchPath = isMatchPath;
        rule.pstate = selectedPState;
        parsed.push_back(rule);
    }

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, "limit-nvpstate", errors.join("\n"));
        return;
    }

    if (parsed.empty()) {
        QMessageBox::warning(this, "limit-nvpstate", "Enter at least one executable name or path");
        return;
    }

    if (isEditing && parsed.size() > 1) {
        QMessageBox::warning(this, "limit-nvpstate", "Only one entry can be edited at a time");
        return;
    }

    result = parsed;
    QDialog::accept();
}
