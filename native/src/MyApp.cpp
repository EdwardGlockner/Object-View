// MyApp.cpp

//*********************************************************************************
// Headers
//*********************************************************************************
#include "../include/MyApp.h"

namespace {

enum {
    ID_MENU_OPEN = wxID_HIGHEST + 1,
    ID_MENU_SAVE_SCREENSHOT,
    ID_MENU_FULL_SCREEN,
    ID_MENU_MODEL_1,
    ID_MENU_MODEL_2,
    ID_MENU_TOGGLE_ROTATION,
    ID_MENU_RESET_ORIENTATION,
    ID_MENU_TOGGLE_WIREFRAME,
    ID_MENU_TOGGLE_DETAIL,
    ID_MENU_TOGGLE_VISIBILITY,
    ID_MENU_CONTROLS,
};

int args[] = {WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 16, 0};

std::filesystem::path parseInitialPath(int argc, wxChar** argv) {
    std::filesystem::path initialPath;

    for (int index = 1; index < argc; index++) {
        const std::string argument = wxString(argv[index]).ToStdString();
        if (argument != "--capture") {
            initialPath = argument;
        }
    }

    return initialPath;
}

}  // namespace

//*********************************************************************************
// Public Class Functions
//*********************************************************************************

//
// OnInit
// Description:
//      Entry point for the wxWidgets application.
//      Creates the GUI (mainframe, menu, left menu and OpenGL canvas).
//      Also initializes all the composition objects used by the native viewer.
// Parameters:
//      None (void).
// Returns:
//      <bool>: Whether the initializing was successful or not.
//
bool MyApp::OnInit() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    frame = new wxFrame((wxFrame*)NULL, -1, wxT("ObjectView"), wxDefaultPosition, wxSize(1500, 920));

    obj_file = "";

    init_menu(mainSizer);
    init_objects();
    init_menubar();

    mainSizer->Add(glPane, 1, wxEXPAND);

    frame->SetMenuBar(menuBar);
    frame->SetSizer(mainSizer);
    frame->SetAutoLayout(true);
    frame->Show();
    frame->Center();

    sync_ui();
    return true;
}

//*********************************************************************************
// Private Class Functions
//*********************************************************************************

//
// init_menubar
// Description:
//      Creates the menubar for the GUI application.
//      Split into a separate function for readability.
//      This function is called from the 'OnInit' function.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void MyApp::init_menubar() {
    menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    wxMenuItem* openFile = fileMenu->Append(ID_MENU_OPEN, "Open...");
    wxMenuItem* saveScreenshot = fileMenu->Append(ID_MENU_SAVE_SCREENSHOT, "Save Screenshot");
    fileMenu->Append(wxID_EXIT, "Exit");
    menuBar->Append(fileMenu, "File");

    wxMenu* viewMenu = new wxMenu();
    wxMenuItem* full_normal_screen = viewMenu->Append(ID_MENU_FULL_SCREEN, "Full / Normal Screen");
    wxMenuItem* model1 = viewMenu->Append(ID_MENU_MODEL_1, "Model 1 - Perseverance Rover");
    wxMenuItem* model2 = viewMenu->Append(ID_MENU_MODEL_2, "Model 2 - Craft Racer");
    menuBar->Append(viewMenu, "View");

    wxMenu* modelMenu = new wxMenu();
    wxMenuItem* toggle_rotation = modelMenu->Append(ID_MENU_TOGGLE_ROTATION, "Toggle Auto Rotation");
    wxMenuItem* toggle_wireframe = modelMenu->Append(ID_MENU_TOGGLE_WIREFRAME, "Toggle Wireframe");
    wxMenuItem* toggle_detail = modelMenu->Append(ID_MENU_TOGGLE_DETAIL, "Toggle Detail Overlay");
    wxMenuItem* toggle_visibility = modelMenu->Append(ID_MENU_TOGGLE_VISIBILITY, "Show / Hide Model");
    menuBar->Append(modelMenu, "Model");

    wxMenu* cameraMenu = new wxMenu();
    wxMenuItem* reset_orientation = cameraMenu->Append(ID_MENU_RESET_ORIENTATION, "Reset Orientation");
    menuBar->Append(cameraMenu, "Camera");

    wxMenu* helpMenu = new wxMenu();
    wxMenuItem* control_help = helpMenu->Append(ID_MENU_CONTROLS, "Controls");
    menuBar->Append(helpMenu, "Help");

    Connect(full_normal_screen->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onFullNormalScreen));
    Connect(model1->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onModel1));
    Connect(model2->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onModel2));
    Connect(toggle_rotation->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onToggleRotation));
    Connect(reset_orientation->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onResetOrientation));
    Connect(toggle_wireframe->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onToggleWireframe));
    Connect(toggle_detail->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onToggleDetail));
    Connect(toggle_visibility->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onToggleVisibility));
    Connect(openFile->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onOpenFile));
    Connect(saveScreenshot->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onSaveScreenshot));
    Connect(control_help->GetId(), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MyApp::onControlHelp));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { frame->Close(true); }, wxID_EXIT);
}

//
// init_objects
// Description:
//      Initializes all the composition objects.
//      Split into a separate function for readability.
//      This function is called from the 'OnInit' function.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void MyApp::init_objects() {
    std::filesystem::path initialPath = parseInitialPath(argc, argv);
    glPane = new GLCanvas((wxFrame*)frame, args, initialPath);
}

//
// init_menu
// Description:
//      Initializes the menu on the left side of the canvas.
//      Split into a separate function for readability.
//      This function is called from the 'OnInit' function.
// Parameters:
//      mainSizer <wxBoxSizer*>: Pointer to the base sizer of the canvas.
// Returns:
//      None (void).
//
void MyApp::init_menu(wxBoxSizer* mainSizer) {
    wxBoxSizer* verticalMenuSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* staticTextMenu = new wxStaticText(frame, wxID_ANY, "Menu");
    staticTextMenu->SetFont(wxFont(26, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    verticalMenuSizer->Add(staticTextMenu, 0, wxALL | wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL, 5);

    wxCoord lineHeight = staticTextMenu->GetFont().GetPointSize() / 10;
    auto addLine = [&](int spacer) {
        wxPanel* linePanel = new wxPanel(frame, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        linePanel->SetMinSize(wxSize(-1, lineHeight));
        linePanel->Bind(wxEVT_PAINT, [linePanel, lineHeight](wxPaintEvent&) {
            wxPaintDC dc(linePanel);
            dc.SetPen(wxPen(wxColour(255, 255, 255), lineHeight, wxPENSTYLE_SOLID));
            dc.DrawLine(0, lineHeight / 2, linePanel->GetSize().GetWidth(), lineHeight / 2);
        });
        verticalMenuSizer->Add(linePanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        if (spacer > 0) {
            verticalMenuSizer->AddSpacer(spacer);
        }
    };

    addLine(20);

    wxStaticText* staticTextChoices = new wxStaticText(frame, wxID_ANY, "Choose model");
    staticTextChoices->SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    verticalMenuSizer->Add(staticTextChoices, 0, wxALL | wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL, 5);

    wxStaticText* staticTextSamples = new wxStaticText(frame, wxID_ANY, "Built-in samples");
    staticTextSamples->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    verticalMenuSizer->Add(staticTextSamples, 0, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);

    sampleChoice = new wxChoice(frame, wxID_ANY);
    sampleChoice->Append("Perseverance Rover");
    sampleChoice->Append("Craft Racer");
    sampleChoice->Append("Toy Car");
    sampleChoice->Append("Textured Cube");
    sampleChoice->SetSelection(0);
    verticalMenuSizer->Add(sampleChoice, 0, wxALL | wxEXPAND, 10);

    wxButton* sample_button = new wxButton(frame, wxID_ANY, "Load Sample");
    verticalMenuSizer->Add(sample_button, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, 10);

    filePicker = new wxFilePickerCtrl(
        frame,
        wxID_ANY,
        wxEmptyString,
        "Select an OBJ file",
        "OBJ files (*.obj)|*.obj",
        wxDefaultPosition,
        wxDefaultSize,
        wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL);
    filePicker->SetToolTip("Choose an OBJ model file.");
    verticalMenuSizer->Add(filePicker, 0, wxALL | wxEXPAND, 10);

    addLine(10);

    wxStaticText* staticTextControls = new wxStaticText(frame, wxID_ANY, "Controls");
    staticTextControls->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    verticalMenuSizer->Add(staticTextControls, 0, wxALL | wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL, 5);

    wxStaticText* staticTextControlsHelp = new wxStaticText(
        frame,
        wxID_ANY,
        "W / A / S / D: translate\nQ / E: zoom\nDrag: rotate\nRight-drag or Shift-drag: pan\nScroll: zoom");
    verticalMenuSizer->Add(staticTextControlsHelp, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

    verticalMenuSizer->Add(0, 1, 1, wxEXPAND);

    addLine(10);

    wxButton* reset_button = new wxButton(frame, wxID_ANY, "Reset");
    wxButton* screenshot_button = new wxButton(frame, wxID_ANY, "Screenshot");
    verticalMenuSizer->Add(reset_button, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
    verticalMenuSizer->Add(screenshot_button, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

    addLine(10);

    titleText = new wxStaticText(frame, wxID_ANY, "Title: -");
    formatText = new wxStaticText(frame, wxID_ANY, "Format: None");
    boundsText = new wxStaticText(frame, wxID_ANY, "Bounds: -");
    triangleText = new wxStaticText(frame, wxID_ANY, "Triangles: 0");
    statusText = new wxStaticText(frame, wxID_ANY, "Status: Load an OBJ model.");
    statusText->Wrap(280);

    verticalMenuSizer->Add(titleText, 0, wxALL, 5);
    verticalMenuSizer->Add(formatText, 0, wxALL, 5);
    verticalMenuSizer->Add(boundsText, 0, wxALL, 5);
    verticalMenuSizer->Add(triangleText, 0, wxALL, 5);
    verticalMenuSizer->Add(statusText, 0, wxALL, 5);

    filePicker->Bind(wxEVT_FILEPICKER_CHANGED, &MyApp::onFileSelected, this);
    sample_button->Bind(wxEVT_BUTTON, &MyApp::onLoadSample, this);
    reset_button->Bind(wxEVT_BUTTON, &MyApp::onReset, this);
    screenshot_button->Bind(wxEVT_BUTTON, &MyApp::onSaveScreenshot, this);

    mainSizer->Add(verticalMenuSizer, 0, wxEXPAND | wxALL, 10);
}

//
// sync_ui
// Description:
//      Synchronizes the sidebar labels with the current state of the OpenGL
//      canvas.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void MyApp::sync_ui() {
    titleText->SetLabel("Title: " + wxString::FromUTF8(glPane->getDisplayTitle().c_str()));
    formatText->SetLabel("Format: " + wxString::FromUTF8(glPane->getDisplayFormat().c_str()));
    boundsText->SetLabel("Bounds: " + wxString::FromUTF8(glPane->getBoundsLabel().c_str()));
    triangleText->SetLabel(wxString::Format("Triangles: %llu", static_cast<unsigned long long>(glPane->getTriangleCount())));
    statusText->SetLabel("Status: " + wxString::FromUTF8(glPane->getStatusText().c_str()));
    statusText->Wrap(280);
}

//
// onFullNormalScreen
// Description:
//      Maximizes the screen size.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onFullNormalScreen(wxCommandEvent&) {
    if (frame == nullptr) {
        return;
    }

    if (frame->IsMaximized()) {
        frame->Restore();
    }
    else {
        frame->Maximize();
    }
}

//
// onModel1
// Description:
//      Updates the model to the first one in the list of models in the
//      'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onModel1(wxCommandEvent&) {
    glPane->setChoosenModel(0);
    sync_ui();
}

//
// onModel2
// Description:
//      Updates the model to the second one in the list of models in the
//      'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onModel2(wxCommandEvent&) {
    glPane->setChoosenModel(1);
    sync_ui();
}

//
// onToggleRotation
// Description:
//      Toggles the autorotation setting in the 'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onToggleRotation(wxCommandEvent&) {
    glPane->setAutoRotate();
    sync_ui();
}

//
// onResetOrientation
// Description:
//      Resets the orientation of the object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onResetOrientation(wxCommandEvent&) {
    glPane->resetOrientation();
    sync_ui();
}

//
// onOpenFile
// Description:
//      Opens a file dialog for choosing an OBJ model.
//      Updates the 'GLCanvas' object to the selected asset.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onOpenFile(wxCommandEvent&) {
    wxFileDialog dialog(
        frame,
        "Open model",
        wxEmptyString,
        wxEmptyString,
        "OBJ files (*.obj)|*.obj|All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    obj_file = dialog.GetPath().ToStdString();
    if (glPane->insertModel(obj_file)) {
        glPane->setChoosenModel(-1);
    }
    sync_ui();
}

//
// onSaveScreenshot
// Description:
//      Saves a screenshot of the OpenGL canvas through the 'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar or button.
// Returns:
//      None (void).
//
void MyApp::onSaveScreenshot(wxCommandEvent&) {
    glPane->saveScreenshot();
    sync_ui();
}

//
// onControlHelp
// Description:
//      Opens a help box for controls.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onControlHelp(wxCommandEvent&) {
    wxMessageBox(
        wxT("W / A / S / D: translate the object\nQ / E: zoom out / zoom in\nDrag: rotate\nRight-drag or Shift-drag: pan\nScroll: zoom\nR: reset\nF: wireframe\nSpace: auto rotate\nThe axis triad shows the model coordinate system.\nUse the Screenshot button or File menu to save a BMP."),
        wxT("Controls"));
}

//
// onToggleWireframe
// Description:
//      Toggles the wireframe setting in the 'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onToggleWireframe(wxCommandEvent&) {
    glPane->toggleWireframe();
    sync_ui();
}

//
// onToggleDetail
// Description:
//      Toggles the detail overlay setting in the 'GLCanvas' object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onToggleDetail(wxCommandEvent&) {
    glPane->toggleDetailOverlay();
    sync_ui();
}

//
// onToggleVisibility
// Description:
//      Toggles whether the current model is visible or not.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the menubar.
// Returns:
//      None (void).
//
void MyApp::onToggleVisibility(wxCommandEvent&) {
    glPane->toggleModelVisibility();
    sync_ui();
}

//
// onFileSelected
// Description:
//      File selector for manually choosing an OBJ model.
//      When the file is selected the 'GLCanvas' object reads it and updates.
// Parameters:
//      event <wxCommandEvent&>: Triggered by choosing a file in the file
//                               selector.
// Returns:
//      None (void).
//
void MyApp::onFileSelected(wxFileDirPickerEvent& event) {
    wxString selectedFilePath = event.GetPath();

    if (selectedFilePath.IsEmpty()) {
        return;
    }

    if (!selectedFilePath.Lower().EndsWith(".obj")) {
        wxMessageBox("Please choose a .obj model file.");
        return;
    }

    obj_file = selectedFilePath.ToStdString();
    if (glPane->insertModel(obj_file)) {
        glPane->setChoosenModel(-1);
    }
    sync_ui();
}

//
// onLoadSample
// Description:
//      Loads one of the bundled native OBJ samples.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by the sample button.
// Returns:
//      None (void).
//
void MyApp::onLoadSample(wxCommandEvent&) {
    if (sampleChoice == nullptr) {
        return;
    }

    const int selection = sampleChoice->GetSelection();
    if (selection == wxNOT_FOUND) {
        return;
    }

    glPane->setChoosenModel(selection);
    sync_ui();
}

//
// onReset
// Description:
//      Resets the orientation of the object.
// Parameters:
//      event <wxCommandEvent&>: Event triggered by pressing the reset button.
// Returns:
//      None (void).
//
void MyApp::onReset(wxCommandEvent&) {
    glPane->resetOrientation();
    sync_ui();
}

IMPLEMENT_APP(MyApp);
