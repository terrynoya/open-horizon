//
// open horizon -- undefined_darkness@outlook.com
//

#pragma once

#include <QMainWindow>
#include <QFormLayout>
#include <QTreeWidget>

//------------------------------------------------------------

class scene_view;

//------------------------------------------------------------

class main_window : public QMainWindow
{
    Q_OBJECT

public:
    explicit main_window(QWidget *parent = 0);

private:
    void setup_menu();

private slots:
    void on_new_mission();
    void on_load_mission();
    void on_save_mission();
    void on_save_as_mission();
    void on_mode_changed(int idx);
    void on_tree_selected(QTreeWidgetItem*, int);
    void on_add_tree_selected(QTreeWidgetItem*, int);

private:
    void update_objects_tree();

private:
    scene_view *m_scene_view;
    QFormLayout *m_edit_layout;
    QTreeWidget *m_objects_tree;
    std::string m_filename;
};

//------------------------------------------------------------