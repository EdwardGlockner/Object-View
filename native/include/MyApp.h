// MyApp.h
// Created by Edward Glockner style adaptation.
// Last modified: 2026-08-21.

//*********************************************************************************
// Header guard
//*********************************************************************************
#ifndef __MYAPP_H
#define __MYAPP_H

//*********************************************************************************
// Headers
//*********************************************************************************
#include <wx/wx.h>
#include <wx/statline.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/statbmp.h>
#include <wx/msgdlg.h>
#include <wx/filepicker.h>
#include <wx/timer.h>
#include <wx/menu.h>
#include <iostream>
#include <string>

#include "GLCanvas.h"

//*********************************************************************************
// Class
//*********************************************************************************
class MyApp: public wxApp
{
    public:
        // Public class functions
        virtual bool OnInit();

        // Public class members
        wxFrame *frame;
        std::string obj_file;

    private:
        // Private class functions
        void init_menubar();
        void init_menu(wxBoxSizer* mainSizer);
        void init_objects();
        void sync_ui();
        void onFullNormalScreen(wxCommandEvent& event);
        void onModel1(wxCommandEvent& event);
        void onModel2(wxCommandEvent& event);
        void onToggleRotation(wxCommandEvent& event);
        void onResetOrientation(wxCommandEvent& event);
        void onOpenFile(wxCommandEvent& event);
        void onSaveScreenshot(wxCommandEvent& event);
        void onControlHelp(wxCommandEvent& event);
        void onToggleWireframe(wxCommandEvent& event);
        void onToggleDetail(wxCommandEvent& event);
        void onToggleVisibility(wxCommandEvent& event);
        void onFileSelected(wxFileDirPickerEvent& event);
        void onLoadSample(wxCommandEvent& event);
        void onReset(wxCommandEvent& event);

        // Private class members
        GLCanvas *glPane;
        wxMenuBar* menuBar;
        wxFilePickerCtrl* filePicker;
        wxChoice* sampleChoice;
        wxStaticText* titleText;
        wxStaticText* formatText;
        wxStaticText* boundsText;
        wxStaticText* triangleText;
        wxStaticText* statusText;
};

#endif
