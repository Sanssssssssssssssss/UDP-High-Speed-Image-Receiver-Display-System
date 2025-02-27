/*
===================================================
Created on: 21-7-2024
Author: Chang Xu
File: mainwindow.cpp
Version: 1.7
Language: C++ (Qt Framework)
Description:
This file defines the main window of the UDP-based
image processing application. It initializes the user
interface and manages the lifecycle of the UI components.
The MainWindow class serves as the central container
for the application, handling initialization and cleanup
of UI elements.
===================================================
*/

#include "mainwindow.h"
#include "ui_mainwindow.h"

/**
 * @brief MainWindow Constructor
 * Initializes the main window and sets up the UI.
 * @param parent The parent widget (default is nullptr)
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

/**
 * @brief MainWindow Destructor
 * Cleans up the UI resources when the main window is destroyed.
 */
MainWindow::~MainWindow()
{
    delete ui;
}


