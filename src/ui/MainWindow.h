#pragma once

#include <QMainWindow>

// Main input-translation window (DESIGN.md §7).
// M0 is an empty shell: a titled window with a centered placeholder label.
// The tabbed layout (Translate / History / Vocabulary) arrives in later milestones.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
};
