/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "ViewerNode.h"

#include <QThread>
#include <QCoreApplication>
#include <QTimer>

#include <ofxNatron.h>

#include "Engine/AppInstance.h"
#include "Engine/AppManager.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/KnobTypes.h"
#include "Engine/Image.h"
#include "Engine/Node.h"
#include "Engine/OpenGLViewerI.h"
#include "Engine/Project.h"
#include "Engine/RenderStats.h"
#include "Engine/RotoPaint.h"
#include "Engine/RotoStrokeItem.h"
#include "Engine/Settings.h"
#include "Engine/TimeLine.h"
#include "Engine/ViewerInstance.h"
#include "Engine/ViewIdx.h"

#include "Serialization/NodeSerialization.h"


/*
 Below are knobs definitions that are not used by the internal ViewerInstance and just used by the Gui
 */

#define kViewerNodeParamLayers "layer"
#define kViewerNodeParamLayersLabel "Layer"
#define kViewerNodeParamLayersHint "The layer that the Viewer node will fetch upstream in the tree. " \
"The channels of the layer will be mapped to the RGBA channels of the viewer according to " \
"its number of channels. (e.g: UV would be mapped to RG)"

#define kViewerNodeParamAlphaChannel "alphaChannel"
#define kViewerNodeParamAlphaChannelLabel "Alpha Channel"
#define kViewerNodeParamAlphaChannelHint "Select here a channel of any layer that will be used when displaying the " \
"alpha channel with the Channels choice on the right"

#define kViewerNodeParamDisplayChannels "displayChannels"
#define kViewerNodeParamDisplayChannelsB "displayChannelsB"
#define kViewerNodeParamDisplayChannelsLabel "Display Channels"
#define kViewerNodeParamDisplayChannelsHint "The channels to display on the viewer from the selected layer"

#define kViewerNodeParamClipToFormat "clipToFormat"
#define kViewerNodeParamClipToFormatLabel "Clip To Format"
#define kViewerNodeParamClipToFormatHint "Clips the portion of the image displayed " \
"on the viewer to the format upstream. When off everything in " \
"region of definition is displayed"

#define kViewerNodeParamFullFrame "fullFrame"
#define kViewerNodeParamFullFrameLabel "Full Frame"
#define kViewerNodeParamFullFrameHint "When checked, the viewer will render the image in its entirety (at full resolution) not just the visible portion. This may be useful when panning/zooming during playback"

#define kViewerNodeParamEnableUserRoI "enableRegionOfInterest"
#define kViewerNodeParamEnableUserRoILabel "Region Of Interest"
#define kViewerNodeParamEnableUserRoIHint "When active, enables the region of interest that limits " \
"the portion of the viewer that is kept updated. Press %2 to create and drag a new region."

#define kViewerNodeParamUserRoIBottomLeft "userRoIBtmLeft"
#define kViewerNodeParamUserRoISize "userRoISize"

#define kViewerNodeParamEnableProxyMode "proxyMode"
#define kViewerNodeParamEnableProxyModeLabel "Proxy Mode"
#define kViewerNodeParamEnableProxyModeHint "Activates the downscaling by the amount indicated by the value on the right. " \
"The rendered images are degraded and as a result of this the whole rendering pipeline is much faster"

#define kViewerNodeParamProxyLevel "proxyLevel"
#define kViewerNodeParamProxyLevelLabel "Proxy Level"
#define kViewerNodeParamProxyLevelHint "When proxy mode is activated, it scales down the rendered image by this factor " \
"to accelerate the rendering"

#define kViewerNodeParamRefreshViewport "refreshViewport"
#define kViewerNodeParamRefreshViewportLabel "Refresh Viewport"
#define kViewerNodeParamRefreshViewportHint "Forces a new render of the current frame. Press %2 to activate in-depth render statistics useful " \
"for debugging the composition"

#define kViewerNodeParamPauseRender "pauseUpdates"
#define kViewerNodeParamPauseRenderB "pauseUpdatesB"
#define kViewerNodeParamPauseRenderLabel "Pause Updates"
#define kViewerNodeParamPauseRenderHint "When activated the viewer will not update after any change that would modify the image " \
"displayed in the viewport. Use %2 to pause both input A and B"

#define kViewerNodeParamAInput "aInput"
#define kViewerNodeParamAInputLabel "A"
#define kViewerNodeParamAInputHint "What node to display in the viewer input A"

#define kViewerNodeParamBInput "bInput"
#define kViewerNodeParamBInputLabel "B"
#define kViewerNodeParamBInputHint "What node to display in the viewer input B"

#define kViewerNodeParamOperation "operation"
#define kViewerNodeParamOperationLabel "Operation"
#define kViewerNodeParamOperationHint "Operation applied between viewer inputs A and B. a and b are the alpha components of each input. d is the wipe dissolve factor, controlled by the arc handle"

#define kViewerNodeParamOperationWipeUnder "Wipe Under"
#define kViewerNodeParamOperationWipeUnderHint "A(1 - d) + Bd"

#define kViewerNodeParamOperationWipeOver "Wipe Over"
#define kViewerNodeParamOperationWipeOverHint "A + B(1 - a)d"

#define kViewerNodeParamOperationWipeMinus "Wipe Minus"
#define kViewerNodeParamOperationWipeMinusHint "A - B"

#define kViewerNodeParamOperationWipeOnionSkin "Wipe Onion skin"
#define kViewerNodeParamOperationWipeOnionSkinHint "A + B"

#define kViewerNodeParamOperationStackUnder "Stack Under"
#define kViewerNodeParamOperationStackUnderHint "B"

#define kViewerNodeParamOperationStackOver "Stack Over"
#define kViewerNodeParamOperationStackOverHint "A + B(1 - a)"

#define kViewerNodeParamOperationStackMinus "Stack Minus"
#define kViewerNodeParamOperationStackMinusHint "A - B"

#define kViewerNodeParamOperationStackOnionSkin "Stack Onion skin"
#define kViewerNodeParamOperationStackOnionSkinHint "A + B"

#define kViewerNodeParamEnableGain "enableGain"
#define kViewerNodeParamEnableGainLabel "Enable Gain"
#define kViewerNodeParamEnableGainHint "Switch between \"neutral\" 1.0 gain f-stop and the previous setting"

#define kViewerNodeParamGain "gain"
#define kViewerNodeParamGainLabel "Gain"
#define kViewerNodeParamGainHint "Gain is shown as f-stops. The image is multipled by pow(2,value) before display"

#define kViewerNodeParamEnableAutoContrast "autoContrast"
#define kViewerNodeParamEnableAutoContrastLabel "Auto Contrast"
#define kViewerNodeParamEnableAutoContrastHint "Automatically adjusts the gain and the offset applied " \
"to the colors of the visible image portion on the viewer"

#define kViewerNodeParamEnableGamma "enableGamma"
#define kViewerNodeParamEnableGammaLabel "Enable Gamma"
#define kViewerNodeParamEnableGammaHint "Gamma correction: Switch between gamma=1.0 and user setting"

#define kViewerNodeParamGamma "gamma"
#define kViewerNodeParamGammaLabel "Gamma"
#define kViewerNodeParamGammaHint "Viewer gamma correction level (applied after gain and before colorspace correction)"

#define kViewerNodeParamColorspace "deviceColorspace"
#define kViewerNodeParamColorspaceLabel "Device Colorspace"
#define kViewerNodeParamColorspaceHint "The operation applied to the image before it is displayed " \
"on screen. The image is converted to this colorspace before being displayed on the monitor"

#define kViewerNodeParamView "activeView"
#define kViewerNodeParamViewLabel "Active View"
#define kViewerNodeParamViewHint "The view displayed on the viewer"

#define kViewerNodeParamZoom "zoom"
#define kViewerNodeParamZoomLabel "Zoom"
#define kViewerNodeParamZoomHint "The zoom applied to the image on the viewer"

#define kViewerNodeParamSyncViewports "syncViewports"
#define kViewerNodeParamSyncViewportsLabel "Sync Viewports"
#define kViewerNodeParamSyncViewportsHint "When enabled, all viewers will be synchronized to the same portion of the image in the viewport"

#define kViewerNodeParamFitViewport "fitViewport"
#define kViewerNodeParamFitViewportLabel "Fit Viewport"
#define kViewerNodeParamFitViewportHint "Scales the image so it doesn't exceed the size of the viewport and centers it"

#define kViewerNodeParamCheckerBoard "enableCheckerBoard"
#define kViewerNodeParamCheckerBoardLabel "Enable Checkerboard"
#define kViewerNodeParamCheckerBoardHint "If checked, the viewer draws a checkerboard under input A instead of black (disabled under the wipe area and in stack modes)"

#define kViewerNodeParamEnableColorPicker "enableInfoBar"
#define kViewerNodeParamEnableColorPickerLabel "Show Info Bar"
#define kViewerNodeParamEnableColorPickerHint "Show/Hide information bar in the bottom of the viewer. If unchecked it also deactivates any active color picker"


// Right-click menu actions

// The right click menu
#define kViewerNodeParamRightClickMenu kNatronOfxParamRightClickMenu

#define kViewerNodeParamRightClickMenuToggleWipe "enableWipeAction"
#define kViewerNodeParamRightClickMenuToggleWipeLabel "Enable Wipe"

#define kViewerNodeParamRightClickMenuCenterWipe "centerWipeAction"
#define kViewerNodeParamRightClickMenuCenterWipeLabel "Center Wipe"

#define kViewerNodeParamRightClickMenuPreviousLayer "previousLayerAction"
#define kViewerNodeParamRightClickMenuPreviousLayerLabel "Previous Layer"

#define kViewerNodeParamRightClickMenuNextLayer "nextLayerAction"
#define kViewerNodeParamRightClickMenuNextLayerLabel "Next Layer"

#define kViewerNodeParamRightClickMenuPreviousView "previousViewAction"
#define kViewerNodeParamRightClickMenuPreviousViewLabel "Previous View"

#define kViewerNodeParamRightClickMenuNextView "nextViewAction"
#define kViewerNodeParamRightClickMenuNextViewLabel "Next View"

#define kViewerNodeParamRightClickMenuSwitchAB "switchABAction"
#define kViewerNodeParamRightClickMenuSwitchABLabel "Switch Input A and B"

#define kViewerNodeParamRightClickMenuShowHideOverlays "showHideOverlays"
#define kViewerNodeParamRightClickMenuShowHideOverlaysLabel "Show/Hide Overlays"

#define kViewerNodeParamRightClickMenuShowHideSubMenu "showHideSubMenu"
#define kViewerNodeParamRightClickMenuShowHideSubMenuLabel "Show/Hide"

#define kViewerNodeParamRightClickMenuHideAll "hideAll"
#define kViewerNodeParamRightClickMenuHideAllLabel "Hide All"

#define kViewerNodeParamRightClickMenuHideAllTop "hideAllTop"
#define kViewerNodeParamRightClickMenuHideAllTopLabel "Hide All Toolbars + Header"

#define kViewerNodeParamRightClickMenuHideAllBottom "hideAllBottom"
#define kViewerNodeParamRightClickMenuHideAllBottomLabel "Hide Player + Timeline"

#define kViewerNodeParamRightClickMenuShowHidePlayer "showHidePlayer"
#define kViewerNodeParamRightClickMenuShowHidePlayerLabel "Show/Hide Player"

#define kViewerNodeParamRightClickMenuShowHideTimeline "showHideTimeline"
#define kViewerNodeParamRightClickMenuShowHideTimelineLabel "Show/Hide Timeline"

#define kViewerNodeParamRightClickMenuShowHideLeftToolbar "showHideLeftToolbar"
#define kViewerNodeParamRightClickMenuShowHideLeftToolbarLabel "Show/Hide Left Toolbar"

#define kViewerNodeParamRightClickMenuShowHideTopToolbar "showHideTopToolbar"
#define kViewerNodeParamRightClickMenuShowHideTopToolbarLabel "Show/Hide Top Toolbar"

#define kViewerNodeParamRightClickMenuShowHideTabHeader "showHideTabHeader"
#define kViewerNodeParamRightClickMenuShowHideTabHeaderLabel "Show/Hide Tab Header"

// Viewer Actions
#define kViewerNodeParamActionLuminance "displayLuminance"
#define kViewerNodeParamActionLuminanceA "displayLuminanceA"
#define kViewerNodeParamActionLuminanceALabel "Display Luminance For A Input Only"
#define kViewerNodeParamActionLuminanceLabel "Display Luminance"

#define kViewerNodeParamActionRed "displayRed"
#define kViewerNodeParamActionRedLabel "Display Red"
#define kViewerNodeParamActionRedA "displayRedA"
#define kViewerNodeParamActionRedALabel "Display Red For A Input Only"

#define kViewerNodeParamActionGreen "displayGreen"
#define kViewerNodeParamActionGreenLabel "Display Green"
#define kViewerNodeParamActionGreenA "displayGreenA"
#define kViewerNodeParamActionGreenALabel "Display Green For A Input Only"

#define kViewerNodeParamActionBlue "displayBlue"
#define kViewerNodeParamActionBlueLabel "Display Blue"
#define kViewerNodeParamActionBlueA "displayBlueA"
#define kViewerNodeParamActionBlueALabel "Display Blue For A Input Only"

#define kViewerNodeParamActionAlpha "displayAlpha"
#define kViewerNodeParamActionAlphaLabel "Display Alpha"
#define kViewerNodeParamActionAlphaA "displayAlphaA"
#define kViewerNodeParamActionAlphaALabel "Display Alpha For A Input Only"

#define kViewerNodeParamActionMatte "displayMatte"
#define kViewerNodeParamActionMatteLabel "Display Matte"
#define kViewerNodeParamActionMatteA "displayMatteA"
#define kViewerNodeParamActionMatteALabel "Display Matte For A Input Only"

#define kViewerNodeParamActionZoomIn "zoomInAction"
#define kViewerNodeParamActionZoomInLabel "Zoom In"

#define kViewerNodeParamActionZoomOut "zoomOut"
#define kViewerNodeParamActionZoomOutLabel "Zoom Out"

#define kViewerNodeParamActionScaleOne "scaleOne"
#define kViewerNodeParamActionScaleOneLabel "Zoom 100%"

#define kViewerNodeParamActionProxy2 "proxy2"
#define kViewerNodeParamActionProxy2Label "Proxy Level 2"

#define kViewerNodeParamActionProxy4 "proxy4"
#define kViewerNodeParamActionProxy4Label "Proxy Level 4"

#define kViewerNodeParamActionProxy8 "proxy8"
#define kViewerNodeParamActionProxy8Label "Proxy Level 8"

#define kViewerNodeParamActionProxy16 "proxy16"
#define kViewerNodeParamActionProxy16Label "Proxy Level 16"

#define kViewerNodeParamActionProxy32 "proxy32"
#define kViewerNodeParamActionProxy32Label "Proxy Level 32"

#define kViewerNodeParamActionLeftView "leftView"
#define kViewerNodeParamActionLeftViewLabel "Left View"

#define kViewerNodeParamActionRightView "rightView"
#define kViewerNodeParamActionRightViewLabel "Right View"

#define kViewerNodeParamActionPauseAB "pauseAB"
#define kViewerNodeParamActionPauseABLabel "Pause input A and B"

#define kViewerNodeParamActionRefreshWithStats "enableStats"
#define kViewerNodeParamActionRefreshWithStatsLabel "Enable Render Stats"

#define kViewerNodeParamActionCreateNewRoI "createNewRoI"
#define kViewerNodeParamActionCreateNewRoILabel "Create New Region Of Interest"

#define kViewerNodeParamActionAbortRender "aboortRender"
#define kViewerNodeParamActionAbortRenderLabel "Abort Rendering"
#define kViewerNodeParamActionAbortRenderHint "Abort any ongoing playback or render"

// Viewer overlay
#define kViewerNodeParamWipeCenter "wipeCenter"
#define kViewerNodeParamWipeAmount "wipeAmount"
#define kViewerNodeParamWipeAngle "wipeAngle"

// Player buttons
#define kViewerNodeParamInPoint "inPoint"
#define kViewerNodeParamInPointLabel "In Point"
#define kViewerNodeParamInPointHint "The playback in point"

#define kViewerNodeParamOutPoint "outPoint"
#define kViewerNodeParamOutPointLabel "Out Point"
#define kViewerNodeParamOutPointHint "The playback out point"

#define kViewerNodeParamEnableFps "enableFps"
#define kViewerNodeParamEnableFpsLabel "Enable FPS"
#define kViewerNodeParamEnableFpsHint "When unchecked, the playback frame rate is automatically set from " \
"the Viewer A input. When checked, the user setting is used"

#define kViewerNodeParamFps "desiredFps"
#define kViewerNodeParamFpsLabel "Fps"
#define kViewerNodeParamFpsHint "Viewer playback framerate, in frames per second"

#define kViewerNodeParamEnableTurboMode "enableTurboMode"
#define kViewerNodeParamEnableTurboModeLabel "Turbo Mode"
#define kViewerNodeParamEnableTurboModeHint "When checked, only the viewer is redrawn during playback, " \
"for maximum efficiency"

#define kViewerNodeParamPlaybackMode "playbackMode"
#define kViewerNodeParamPlaybackModeLabel "Playback Mode"
#define kViewerNodeParamPlaybackModeHint "Behavior to adopt when the playback hit the end of the range: loop,bounce or stop"

#define kViewerNodeParamSyncTimelines "syncTimelines"
#define kViewerNodeParamSyncTimelinesLabel "Sync Timelines"
#define kViewerNodeParamSyncTimelinesHint "When activated, the timeline frame-range is synchronized with the Dope Sheet and the Curve Editor"



#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif
#ifndef M_PI_2
#define M_PI_2      1.57079632679489661923132169163975144   /* pi/2           */
#endif
#ifndef M_PI_4
#define M_PI_4      0.785398163397448309615660845819875721  /* pi/4           */
#endif


#define VIEWER_UI_SECTIONS_SPACING_PX 5


#define WIPE_MIX_HANDLE_LENGTH 50.
#define WIPE_ROTATE_HANDLE_LENGTH 100.
#define WIPE_ROTATE_OFFSET 30


#define USER_ROI_BORDER_TICK_SIZE 15.f
#define USER_ROI_CROSS_RADIUS 15.f
#define USER_ROI_SELECTION_POINT_SIZE 8.f
#define USER_ROI_CLICK_TOLERANCE 8.f

#define VIEWER_INITIAL_N_INPUTS 10


enum ViewerNodeInteractMouseStateEnum
{
    eViewerNodeInteractMouseStateIdle,
    eViewerNodeInteractMouseStateBuildingUserRoI,
    eViewerNodeInteractMouseStateDraggingRoiLeftEdge,
    eViewerNodeInteractMouseStateDraggingRoiRightEdge,
    eViewerNodeInteractMouseStateDraggingRoiTopEdge,
    eViewerNodeInteractMouseStateDraggingRoiBottomEdge,
    eViewerNodeInteractMouseStateDraggingRoiTopLeft,
    eViewerNodeInteractMouseStateDraggingRoiTopRight,
    eViewerNodeInteractMouseStateDraggingRoiBottomRight,
    eViewerNodeInteractMouseStateDraggingRoiBottomLeft,
    eViewerNodeInteractMouseStateDraggingRoiCross,
    eViewerNodeInteractMouseStateDraggingWipeCenter,
    eViewerNodeInteractMouseStateDraggingWipeMixHandle,
    eViewerNodeInteractMouseStateRotatingWipeHandle
};

enum HoverStateEnum
{
    eHoverStateNothing = 0,
    eHoverStateWipeMix,
    eHoverStateWipeRotateHandle
};

NATRON_NAMESPACE_ENTER;


PluginPtr
ViewerNode::createPlugin()
{
    std::vector<std::string> grouping;
    grouping.push_back(PLUGIN_GROUP_IMAGE);
    PluginPtr ret = Plugin::create((void*)ViewerNode::create, PLUGINID_NATRON_VIEWER_GROUP, "Viewer", 1, 0, grouping);

    ret->setProperty<std::string>(kNatronPluginPropIconFilePath, "Images/viewer_icon.png");
    QString desc = tr("The Viewer node can display the output of a node graph. Shift + double click on the viewer node to customize the viewer display process with a custom node tree");

    ret->setProperty<int>(kNatronPluginPropRenderSafety, eRenderSafetyFullySafe);
    ret->setProperty<std::string>(kNatronPluginPropDescription, desc.toStdString());
    ret->setProperty<int>(kNatronPluginPropShortcut, (int)Key_I, 0);
    ret->setProperty<int>(kNatronPluginPropShortcut, (int)eKeyboardModifierControl, 1
                          );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamClipToFormat, kViewerNodeParamClipToFormatLabel, Key_C, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamFullFrame, kViewerNodeParamFullFrameLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableUserRoI, kViewerNodeParamEnableUserRoILabel, Key_W, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableProxyMode, kViewerNodeParamEnableProxyModeLabel, Key_P, eKeyboardModifierControl) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPauseRender, kViewerNodeParamPauseRenderLabel, Key_P) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableGain, kViewerNodeParamEnableGainLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableAutoContrast, kViewerNodeParamEnableAutoContrastLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableGamma, kViewerNodeParamEnableGammaLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRefreshViewport, kViewerNodeParamRefreshViewportLabel, Key_U) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamFitViewport, kViewerNodeParamFitViewportLabel, Key_F) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamSyncViewports, kViewerNodeParamSyncViewportsLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamCheckerBoard, kViewerNodeParamCheckerBoardLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamEnableColorPicker, kViewerNodeParamEnableColorPickerLabel) );

    // Right-click actions
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuToggleWipe, kViewerNodeParamRightClickMenuToggleWipeLabel, Key_W) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuCenterWipe, kViewerNodeParamRightClickMenuCenterWipeLabel, Key_F, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuPreviousLayer, kViewerNodeParamRightClickMenuPreviousLayerLabel, Key_Page_Up) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuNextLayer, kViewerNodeParamRightClickMenuNextLayerLabel, Key_Page_Down) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuSwitchAB, kViewerNodeParamRightClickMenuSwitchABLabel, Key_Return) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuPreviousView, kViewerNodeParamRightClickMenuPreviousViewLabel, Key_Page_Up, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuNextView, kViewerNodeParamRightClickMenuNextViewLabel, Key_Page_Down, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHideOverlays, kViewerNodeParamRightClickMenuShowHideOverlaysLabel, Key_O) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuHideAll, kViewerNodeParamRightClickMenuHideAllLabel, Key_space, KeyboardModifiers(eKeyboardModifierShift | eKeyboardModifierAlt) ));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuHideAllTop, kViewerNodeParamRightClickMenuHideAllTopLabel, Key_space, eKeyboardModifierShift ));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuHideAllBottom, kViewerNodeParamRightClickMenuHideAllBottomLabel, Key_space, eKeyboardModifierAlt ));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHidePlayer, kViewerNodeParamRightClickMenuShowHidePlayerLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHideTimeline, kViewerNodeParamRightClickMenuShowHideTimelineLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHideLeftToolbar, kViewerNodeParamRightClickMenuShowHideLeftToolbarLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHideTopToolbar, kViewerNodeParamRightClickMenuShowHideTopToolbarLabel) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamRightClickMenuShowHideTabHeader, kViewerNodeParamRightClickMenuShowHideTabHeaderLabel) );

    // Viewer actions
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionLuminance, kViewerNodeParamActionLuminanceLabel, Key_Y) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionRed, kViewerNodeParamActionRedLabel, Key_R) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionRedA, kViewerNodeParamActionRedALabel, Key_R, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionGreen, kViewerNodeParamActionGreenLabel, Key_G) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionGreenA, kViewerNodeParamActionGreenALabel, Key_G, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionBlue, kViewerNodeParamActionBlueLabel, Key_B) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionBlueA, kViewerNodeParamActionBlueALabel, Key_B, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionAlpha, kViewerNodeParamActionAlphaLabel, Key_A) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionAlphaA, kViewerNodeParamActionAlphaALabel, Key_A, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionMatte, kViewerNodeParamActionMatteLabel, Key_M) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionMatteA, kViewerNodeParamActionMatteALabel, Key_M, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionZoomIn, kViewerNodeParamActionZoomInLabel, Key_plus) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionZoomOut, kViewerNodeParamActionZoomOutLabel, Key_minus) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionScaleOne, kViewerNodeParamActionScaleOneLabel, Key_1, eKeyboardModifierControl) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionProxy2, kViewerNodeParamActionProxy2Label, Key_1, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionProxy4, kViewerNodeParamActionProxy4Label, Key_2, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionProxy8, kViewerNodeParamActionProxy8Label, Key_3, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionProxy16, kViewerNodeParamActionProxy16Label, Key_4, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionProxy32, kViewerNodeParamActionProxy32Label, Key_5, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionLeftView, kViewerNodeParamActionLeftViewLabel, Key_Left, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionRightView, kViewerNodeParamActionRightViewLabel, Key_Right, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionCreateNewRoI, kViewerNodeParamActionCreateNewRoILabel, Key_W, eKeyboardModifierAlt) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionPauseAB, kViewerNodeParamActionPauseABLabel, Key_P, eKeyboardModifierShift) );
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionRefreshWithStats, kViewerNodeParamActionRefreshWithStatsLabel, Key_U, KeyboardModifiers(eKeyboardModifierShift | eKeyboardModifierControl)) );


    // Player
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPreviousFrame, kViewerNodeParamPreviousFrameLabel, Key_Left, eKeyboardModifierNone));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamNextFrame, kViewerNodeParamNextFrameLabel, Key_Right, eKeyboardModifierNone));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPlayBackward, kViewerNodeParamPlayBackwardLabel, Key_J, eKeyboardModifierNone));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPlayForward, kViewerNodeParamPlayForwardLabel, Key_L, eKeyboardModifierNone));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamActionAbortRender, kViewerNodeParamActionAbortRenderLabel, Key_K, eKeyboardModifierNone));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPreviousIncr, kViewerNodeParamPreviousIncrLabel, Key_Left, eKeyboardModifierShift));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamNextIncr, kViewerNodeParamNextIncrLabel, Key_Right, eKeyboardModifierShift));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamPreviousKeyFrame, kViewerNodeParamPreviousKeyFrameLabel, Key_Left, KeyboardModifiers(eKeyboardModifierShift | eKeyboardModifierControl)));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamNextKeyFrame, kViewerNodeParamNextKeyFrameLabel, Key_Right, KeyboardModifiers(eKeyboardModifierShift | eKeyboardModifierControl)));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamFirstFrame, kViewerNodeParamFirstFrameLabel, Key_Left, eKeyboardModifierControl));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamLastFrame, kViewerNodeParamLastFrameLabel, Key_Right, eKeyboardModifierControl));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamSetInPoint, kViewerNodeParamSetInPointLabel, Key_I, eKeyboardModifierAlt));
    ret->addActionShortcut( PluginActionShortcut(kViewerNodeParamSetOutPoint, kViewerNodeParamSetOutPointLabel, Key_O, eKeyboardModifierAlt));


    return ret;
}

struct ViewerNodePrivate
{
    ViewerNode* _publicInterface;

    // Pointer to ViewerGL (interface)
    OpenGLViewerI* uiContext;

    NodeWPtr internalViewerProcessNode;

    boost::weak_ptr<KnobChoice> layersKnob;
    boost::weak_ptr<KnobChoice> alphaChannelKnob;
    boost::weak_ptr<KnobChoice> displayChannelsKnob[2];
    boost::weak_ptr<KnobChoice> zoomChoiceKnob;
    boost::weak_ptr<KnobButton> syncViewersButtonKnob;
    boost::weak_ptr<KnobButton> centerViewerButtonKnob;
    boost::weak_ptr<KnobButton> clipToFormatButtonKnob;
    boost::weak_ptr<KnobButton> fullFrameButtonKnob;
    boost::weak_ptr<KnobButton> toggleUserRoIButtonKnob;
    boost::weak_ptr<KnobDouble> userRoIBtmLeftKnob;
    boost::weak_ptr<KnobDouble> userRoISizeKnob;
    boost::weak_ptr<KnobButton> toggleProxyModeButtonKnob;
    boost::weak_ptr<KnobChoice> proxyChoiceKnob;
    boost::weak_ptr<KnobButton> refreshButtonKnob;
    boost::weak_ptr<KnobButton> pauseButtonKnob[2];
    boost::weak_ptr<KnobChoice> aInputNodeChoiceKnob;
    boost::weak_ptr<KnobChoice> blendingModeChoiceKnob;
    boost::weak_ptr<KnobChoice> bInputNodeChoiceKnob;

    boost::weak_ptr<KnobButton> enableGainButtonKnob;
    boost::weak_ptr<KnobDouble> gainSliderKnob;
    boost::weak_ptr<KnobButton> enableAutoContrastButtonKnob;
    boost::weak_ptr<KnobButton> enableGammaButtonKnob;
    boost::weak_ptr<KnobDouble> gammaSliderKnob;
    boost::weak_ptr<KnobButton> enableCheckerboardButtonKnob;
    boost::weak_ptr<KnobChoice> colorspaceKnob;
    boost::weak_ptr<KnobChoice> activeViewKnob;
    boost::weak_ptr<KnobButton> enableInfoBarButtonKnob;

    // Player
    boost::weak_ptr<KnobButton> setInPointButtonKnob;
    boost::weak_ptr<KnobButton> setOutPointButtonKnob;
    boost::weak_ptr<KnobInt> inPointKnob;
    boost::weak_ptr<KnobInt> outPointKnob;
    boost::weak_ptr<KnobInt> curFrameKnob;
    boost::weak_ptr<KnobBool> enableFpsKnob;
    boost::weak_ptr<KnobDouble> fpsKnob;
    boost::weak_ptr<KnobButton> enableTurboModeButtonKnob;
    boost::weak_ptr<KnobChoice> playbackModeKnob;
    boost::weak_ptr<KnobButton> syncTimelinesButtonKnob;
    boost::weak_ptr<KnobButton> firstFrameButtonKnob;
    boost::weak_ptr<KnobButton> playBackwardButtonKnob;
    boost::weak_ptr<KnobButton> playForwardButtonKnob;
    boost::weak_ptr<KnobButton> lastFrameButtonKnob;
    boost::weak_ptr<KnobButton> prevFrameButtonKnob;
    boost::weak_ptr<KnobButton> nextFrameButtonKnob;
    boost::weak_ptr<KnobButton> prevKeyFrameButtonKnob;
    boost::weak_ptr<KnobButton> nextKeyFrameButtonKnob;
    boost::weak_ptr<KnobButton> prevIncrButtonKnob;
    boost::weak_ptr<KnobInt> incrFrameKnob;
    boost::weak_ptr<KnobButton> nextIncrButtonKnob;


    // Overlays
    boost::weak_ptr<KnobDouble> wipeCenter;
    boost::weak_ptr<KnobDouble> wipeAmount;
    boost::weak_ptr<KnobDouble> wipeAngle;

    // Right click menu
    boost::weak_ptr<KnobChoice> rightClickMenu;
    boost::weak_ptr<KnobButton> rightClickToggleWipe;
    boost::weak_ptr<KnobButton> rightClickCenterWipe;
    boost::weak_ptr<KnobButton> rightClickPreviousLayer;
    boost::weak_ptr<KnobButton> rightClickNextLayer;
    boost::weak_ptr<KnobButton> rightClickPreviousView;
    boost::weak_ptr<KnobButton> rightClickNextView;
    boost::weak_ptr<KnobButton> rightClickSwitchAB;
    boost::weak_ptr<KnobButton> rightClickShowHideOverlays;
    boost::weak_ptr<KnobChoice> rightClickShowHideSubMenu;
    boost::weak_ptr<KnobButton> rightClickHideAll;
    boost::weak_ptr<KnobButton> rightClickHideAllTop;
    boost::weak_ptr<KnobButton> rightClickHideAllBottom;
    boost::weak_ptr<KnobButton> rightClickShowHidePlayer;
    boost::weak_ptr<KnobButton> rightClickShowHideTimeline;
    boost::weak_ptr<KnobButton> rightClickShowHideLeftToolbar;
    boost::weak_ptr<KnobButton> rightClickShowHideTopToolbar;
    boost::weak_ptr<KnobButton> rightClickShowHideTabHeader;

    // Viewer actions
    boost::weak_ptr<KnobButton> displayLuminanceAction[2];
    boost::weak_ptr<KnobButton> displayRedAction[2];
    boost::weak_ptr<KnobButton> displayGreenAction[2];
    boost::weak_ptr<KnobButton> displayBlueAction[2];
    boost::weak_ptr<KnobButton> displayAlphaAction[2];
    boost::weak_ptr<KnobButton> displayMatteAction[2];
    boost::weak_ptr<KnobButton> zoomInAction;
    boost::weak_ptr<KnobButton> zoomOutAction;
    boost::weak_ptr<KnobButton> zoomScaleOneAction;
    boost::weak_ptr<KnobButton> proxyLevelAction[5];
    boost::weak_ptr<KnobButton> leftViewAction;
    boost::weak_ptr<KnobButton> rightViewAction;
    boost::weak_ptr<KnobButton> pauseABAction;
    boost::weak_ptr<KnobButton> enableStatsAction;
    boost::weak_ptr<KnobButton> createUserRoIAction;
    boost::weak_ptr<KnobButton> abortRenderingAction;

    double lastFstopValue;
    double lastGammaValue;
    int lastWipeIndex;
    RectD draggedUserRoI;
    bool buildUserRoIOnNextPress;
    ViewerNodeInteractMouseStateEnum uiState;
    HoverStateEnum hoverState;
    QPointF lastMousePos;

    struct ViewerInput
    {
        std::string label;
        NodeWPtr node;
    };
    std::vector<ViewerInput> viewerInputs;

    QTimer mustSetUpPlaybackButtonsTimer;


    ViewerNodePrivate(ViewerNode* publicInterface)
    : _publicInterface(publicInterface)
    , uiContext(0)
    , lastFstopValue(0)
    , lastGammaValue(1)
    , lastWipeIndex(0)
    , draggedUserRoI()
    , buildUserRoIOnNextPress(false)
    , uiState(eViewerNodeInteractMouseStateIdle)
    , hoverState(eHoverStateNothing)
    , lastMousePos()
    {

    }

    NodePtr getInternalViewerNode() const
    {
        return internalViewerProcessNode.lock();
    }

    void onInternalViewerCreated();

    void drawWipeControl();

    void drawUserRoI();

    void drawArcOfCircle(const QPointF & center, double radiusX, double radiusY, double startAngle, double endAngle);

    void showRightClickMenu();

    static bool isNearbyWipeCenter(const QPointF& wipeCenter, const QPointF & pos, double zoomScreenPixelWidth, double zoomScreenPixelHeight ) ;

    static bool isNearbyWipeRotateBar(const QPointF& wipeCenter, double wipeAngle, const QPointF & pos, double zoomScreenPixelWidth, double zoomScreenPixelHeight);

    static bool isNearbyWipeMixHandle(const QPointF& wipeCenter, double wipeAngle, double mixAmount, const QPointF & pos, double zoomScreenPixelWidth, double zoomScreenPixelHeight);

    static bool isNearByUserRoITopEdge(const RectD & roi,
                                const QPointF & zoomPos,
                                double zoomScreenPixelWidth,
                                double zoomScreenPixelHeight);

    static bool isNearByUserRoIRightEdge(const RectD & roi,
                                  const QPointF & zoomPos,
                                  double zoomScreenPixelWidth,
                                  double zoomScreenPixelHeight);

    static bool isNearByUserRoILeftEdge(const RectD & roi,
                                 const QPointF & zoomPos,
                                 double zoomScreenPixelWidth,
                                 double zoomScreenPixelHeight);


    static bool isNearByUserRoIBottomEdge(const RectD & roi,
                                   const QPointF & zoomPos,
                                   double zoomScreenPixelWidth,
                                   double zoomScreenPixelHeight);

    static bool isNearByUserRoI(double x,
                         double y,
                         const QPointF & zoomPos,
                         double zoomScreenPixelWidth,
                         double zoomScreenPixelHeight);

    void refreshInputChoices(bool resetChoiceIfNotFound);

    void scaleZoomFactor(double scale) {
        double factor = uiContext->getZoomFactor();
        factor *= scale;
        uiContext->zoomViewport(factor);
    }

    void getAllViewerNodes(bool inGroupOnly, std::list<ViewerNodePtr>& viewerNodes) const;

    void abortAllViewersRendering();

    void startPlayback(RenderDirectionEnum direction);

    void timelineGoTo(double time);

    void refreshInputChoiceMenu(int internalIndex, int groupInputIndex);

};

EffectInstancePtr
ViewerNode::create(const NodePtr& node)
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    return EffectInstancePtr( new ViewerNode(node) );
}

ViewerNode::ViewerNode(const NodePtr& node)
    : NodeGroup(node)
    , _imp( new ViewerNodePrivate(this) )
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    setSupportsRenderScaleMaybe(EffectInstance::eSupportsYes);

    QObject::connect( this, SIGNAL(disconnectTextureRequest(int,bool)), this, SLOT(executeDisconnectTextureRequestOnMainThread(int,bool)) );
    QObject::connect( this, SIGNAL(redrawOnMainThread()), this, SLOT(redrawViewer()) );

    if (node) {
        QObject::connect( node.get(), SIGNAL(inputLabelChanged(int,QString)), this, SLOT(onInputNameChanged(int,QString)) );
    }

}

ViewerNode::~ViewerNode()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    if (_imp->uiContext) {
        _imp->uiContext->removeGUI();
    }
}

ViewerInstancePtr
ViewerNode::getInternalViewerNode() const
{
    NodePtr node = _imp->getInternalViewerNode();
    if (!node) {
        return ViewerInstancePtr();
    }
    return toViewerInstance(node->getEffectInstance());
}


void
ViewerNodePrivate::refreshInputChoices(bool resetChoiceIfNotFound)
{
    // Refresh the A and B input choices
    ViewerInstancePtr internalInstance = toViewerInstance(getInternalViewerNode()->getEffectInstance());
    KnobChoicePtr aInputKnob = aInputNodeChoiceKnob.lock();
    KnobChoicePtr bInputKnob = bInputNodeChoiceKnob.lock();
    std::string aCurChoice = aInputKnob->getActiveEntryText_mt_safe();
    std::string bCurChoice = bInputKnob->getActiveEntryText_mt_safe();

    if (aCurChoice == "-") {
        aCurChoice.clear();
    }
    if (bCurChoice == "-") {
        bCurChoice.clear();
    }

    ViewerCompositingOperatorEnum operation = (ViewerCompositingOperatorEnum)blendingModeChoiceKnob.lock()->getValue();
    bInputKnob->setAllDimensionsEnabled(operation != eViewerCompositingOperatorNone);

    // If we found the old choice and the old choice it not "-", we set the index
    int foundCurAChoiceIndex = -1;
    int foundCurBChoiceIndex = -1;

    std::vector<std::string> entries;
    entries.push_back("-");
    int nInputs = _publicInterface->getMaxInputCount();
    viewerInputs.clear();
    viewerInputs.resize(nInputs);
    for (int i = 0; i < nInputs; ++i) {
        NodePtr inputNode = _publicInterface->getNode()->getRealInput(i);
        if (!inputNode) {
            continue;
        }

        entries.push_back(inputNode->getLabel());

        viewerInputs[i].label = entries.back();
        viewerInputs[i].node = inputNode;

        if (foundCurAChoiceIndex == -1 && !aCurChoice.empty() && aCurChoice == entries.back()) {
            foundCurAChoiceIndex = entries.size() - 1;
        }

        if (foundCurBChoiceIndex == -1 && !bCurChoice.empty()  && bCurChoice == entries.back()) {
            foundCurBChoiceIndex = entries.size() - 1;
        }
    }

    aInputKnob->populateChoices(entries);
    bInputKnob->populateChoices(entries);

    // Restore old choices

    if (foundCurAChoiceIndex != -1) {
        if (foundCurAChoiceIndex == 0) {
            aInputKnob->setValue(entries.size() > 1 ? 1 : foundCurAChoiceIndex);
        } else {
            aInputKnob->setValue(foundCurAChoiceIndex);
        }

    } else {
        if (resetChoiceIfNotFound) {
            aInputKnob->setValue(entries.size() > 1 ? 1 : 0);
        }
    }
    if (foundCurBChoiceIndex != -1) {
        if (foundCurBChoiceIndex == 0) {
            bInputKnob->setValue(entries.size() > 1 ? 1 : foundCurBChoiceIndex);
        } else {
            bInputKnob->setValue(foundCurBChoiceIndex);
        }
    } else {
        if (resetChoiceIfNotFound) {
            bInputKnob->setValue(entries.size() > 1 ? 1 : 0);
        }
    }


    if (uiContext) {
        if ( (operation == eViewerCompositingOperatorNone) || !bInputKnob->isEnabled(0)  || bCurChoice.empty() ) {
            uiContext->setInfoBarVisible(1, false);
        } else if (operation != eViewerCompositingOperatorNone) {
            uiContext->setInfoBarVisible(1, true);
        }
    }

    /*bool autoWipe = getInternalNode()->isInputChangeRequestedFromViewer();

     if ( autoWipe && (activeInputs[0] != -1) && (activeInputs[1] != -1) && (activeInputs[0] != activeInputs[1])
     && (operation == eViewerCompositingOperatorNone) ) {
     blendingModeChoiceKnob.lock()->setValue((int)eViewerCompositingOperatorWipeUnder);
     }*/

} // refreshInputChoices

void
ViewerNode::onInputChanged(int /*inputNb*/)
{
    _imp->refreshInputChoices(true);
}

void
ViewerNode::onInputNameChanged(int,QString)
{
    _imp->refreshInputChoices(false);
}

void
ViewerNode::onKnobsLoaded()
{
    _imp->lastGammaValue = _imp->gammaSliderKnob.lock()->getValue();
    _imp->lastFstopValue = _imp->gainSliderKnob.lock()->getValue();
}

void
ViewerNode::createViewerProcessNode()
{
    NodePtr internalViewerNode;
    {
        ViewerNodePtr thisShared = shared_from_this();

        QString nodeName = QString::fromUtf8("ViewerProcess");
        CreateNodeArgsPtr args(CreateNodeArgs::create(PLUGINID_NATRON_VIEWER_INTERNAL, thisShared));
        //args.setProperty<bool>(kCreateNodeArgsPropNoNodeGUI, true);
        args->setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        args->setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        args->setProperty<bool>(kCreateNodeArgsPropAllowNonUserCreatablePlugins, true);
        args->setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        args->setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, nodeName.toStdString());
        internalViewerNode = getApp()->createNode(args);

    }
    assert(internalViewerNode);
    if (!internalViewerNode) {
        throw std::invalid_argument("ViewerNode::setupGraph: No internal viewer process!");
    }
    _imp->internalViewerProcessNode = internalViewerNode;
    _imp->onInternalViewerCreated();
    Q_EMIT internalViewerCreated();
}

void
ViewerNode::setupGraph(bool createViewerProcess)
{
    // Viewers are not considered edited by default
    setSubGraphEditedByUser(false);

    ViewerNodePtr thisShared = shared_from_this();

    NodePtr internalViewerNode = _imp->internalViewerProcessNode.lock();
    assert(createViewerProcess || internalViewerNode);
    if (createViewerProcess) {
        createViewerProcessNode();
        internalViewerNode = _imp->internalViewerProcessNode.lock();
    }


    double inputWidth = 1, inputHeight = 1;
    if (internalViewerNode) {
        internalViewerNode->getSize(&inputWidth, &inputHeight);
    }
    double inputX = 0, inputY = 0;
    if (internalViewerNode) {
        internalViewerNode->getPosition(&inputX, &inputY);
    }

    double startOffset = - (VIEWER_INITIAL_N_INPUTS / 2) * inputWidth - inputWidth / 2. - (VIEWER_INITIAL_N_INPUTS / 2 - 1) * inputWidth / 2;

    std::vector<NodePtr> inputNodes(VIEWER_INITIAL_N_INPUTS);
    // Create input nodes
    for (int i = 0; i < VIEWER_INITIAL_N_INPUTS; ++i) {
        QString inputName = QString::fromUtf8("Input%1").arg(i + 1);
        CreateNodeArgsPtr args(CreateNodeArgs::create(PLUGINID_NATRON_INPUT, thisShared));
        args->setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        args->setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        args->setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        args->setProperty<std::string>(kCreateNodeArgsPropNodeInitialName, inputName.toStdString());
        args->setProperty<double>(kCreateNodeArgsPropNodeInitialPosition, inputX + startOffset, 0);
        args->setProperty<double>(kCreateNodeArgsPropNodeInitialPosition, inputY - inputHeight * 10, 1);
        //args.addParamDefaultValue<bool>(kNatronGroupInputIsOptionalParamName, true);
        inputNodes[i] = getApp()->createNode(args);
        assert(inputNodes[i]);
        startOffset += inputWidth * 1.5;
    }

}

void
ViewerNode::setupInitialSubGraphState()
{
    setupGraph(true);
}

void
ViewerNode::clearGroupWithoutViewerProcess()
{
    // When we load the internal node-graph we don't want to kill the viewer process node, hence we remove it temporarily from the group so it doesn't get killed
    // and the re-add it back.
    if (getNodes().empty()) {
        return;
    }
    NodePtr viewerProcessNode = _imp->internalViewerProcessNode.lock();
    assert(viewerProcessNode);
    removeNode(viewerProcessNode);
    clearNodesBlocking();
    addNode(viewerProcessNode);
}

void
ViewerNode::loadSubGraph(const SERIALIZATION_NAMESPACE::NodeSerialization* projectSerialization,
                         const SERIALIZATION_NAMESPACE::NodeSerialization* pyPlugSerialization)
{


    if (getNode()->isPyPlug()) {
        assert(pyPlugSerialization);
        // Handle the pyplug case on NodeGroup implementation
        NodeGroup::loadSubGraph(projectSerialization, pyPlugSerialization);
    } else if (projectSerialization) {
        // If there's a project serialization load it. There will be children only if the user edited the Viewer group
        if (!projectSerialization->_children.empty()) {

            // The subgraph was not initialized in this case
            assert(getNodes().empty());

            EffectInstancePtr thisShared = shared_from_this();

            // Clear nodes that were created if any
            clearGroupWithoutViewerProcess();

            // Restore group
            Project::restoreGroupFromSerialization(projectSerialization->_children, toNodeGroup(thisShared));

            setSubGraphEditedByUser(true);
        } else {

            // We are loading a non edited default viewer, just ensure the initial setup was done
            if (!getInternalViewerNode()) {
                setupGraph(true);
            }

            setSubGraphEditedByUser(false);
        }
    }



    // Ensure the internal viewer process node exists
    if (!_imp->internalViewerProcessNode.lock()) {
        NodePtr internalViewerNode;
        NodesList nodes = getNodes();
        for (NodesList::iterator it = nodes.begin(); it!=nodes.end(); ++it) {
            if ((*it)->isEffectViewerInstance()) {
                internalViewerNode = *it;
                break;
            }
        }
        assert(internalViewerNode);
        if (!internalViewerNode) {
            throw std::invalid_argument("ViewerNode::onGroupCreated: No internal viewer process!");
        }
        _imp->internalViewerProcessNode = internalViewerNode;
        Q_EMIT internalViewerCreated();
        
        _imp->onInternalViewerCreated();

    }
    assert(getInternalViewerNode());


    _imp->refreshInputChoices(true);
    refreshInputFromChoiceMenu(0);
    refreshInputFromChoiceMenu(1);
}


void
ViewerNodePrivate::onInternalViewerCreated()
{
    ViewerInstancePtr viewerNode = internalViewerProcessNode.lock()->isEffectViewerInstance();
    RenderEnginePtr engine = viewerNode->getRenderEngine();
    QObject::connect( engine.get(), SIGNAL(renderFinished(int)), _publicInterface, SLOT(onEngineStopped()) );
    QObject::connect( engine.get(), SIGNAL(renderStarted(bool)), _publicInterface, SLOT(onEngineStarted(bool)) );

    // Refresh visibility & enabledness
    fpsKnob.lock()->setAllDimensionsEnabled(enableFpsKnob.lock()->getValue());

    // Refresh playback mode
    PlaybackModeEnum mode = (PlaybackModeEnum)playbackModeKnob.lock()->getValue();
    engine->setPlaybackMode(mode);

    // Refresh fps
    engine->setDesiredFPS(fpsKnob.lock()->getValue());

    mustSetUpPlaybackButtonsTimer.setSingleShot(true);
    QObject::connect( &mustSetUpPlaybackButtonsTimer, SIGNAL(timeout()), _publicInterface, SLOT(onSetDownPlaybackButtonsTimeout()) );

}

/**
 * @brief Creates a duplicate of the knob identified by knobName which is a knob in the internalNode onto the effect and add it to the given page.
 **/
template <typename KNOBTYPE>
boost::shared_ptr<KNOBTYPE>
createDuplicateKnob( const std::string& knobName,
                    const NodePtr& internalNode,
                    const EffectInstancePtr& effect,
                    const KnobPagePtr& page = KnobPagePtr(),
                    const KnobGroupPtr& group = KnobGroupPtr() )
{
    KnobIPtr internalNodeKnob = internalNode->getKnobByName(knobName);
    KnobIPtr duplicateKnob = internalNodeKnob->createDuplicateOnHolder(effect, page, group, -1, true, internalNodeKnob->getName(), internalNodeKnob->getLabel(), internalNodeKnob->getHintToolTip(), false, false);
    return boost::dynamic_pointer_cast<KNOBTYPE>(duplicateKnob);
}

void
ViewerNode::initializeKnobs()
{

    ViewerNodePtr thisShared = shared_from_this();

    

    KnobPagePtr page = AppManager::createKnob<KnobPage>( thisShared, tr("UIControls") );
    page->setName("viewerUIControls");
    page->setSecret(true);

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamLayersLabel) );
        param->setName(kViewerNodeParamLayers);
        param->setHintToolTip(tr(kViewerNodeParamLayersHint));
        param->setMissingEntryWarningEnabled(false);
        {
            std::vector<std::string> entries;
            entries.push_back("-");
            param->populateChoices(entries);
        }
        page->addKnob(param);
        param->setSecret(true);
        param->setTextToFitHorizontally("Color.Toto.RGBA");
        _imp->layersKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamAlphaChannelLabel) );
        param->setName(kViewerNodeParamAlphaChannel);
        param->setHintToolTip(tr(kViewerNodeParamAlphaChannelHint));
        param->setMissingEntryWarningEnabled(false);
        {
            std::vector<std::string> entries;
            entries.push_back("-");
            param->populateChoices(entries);
        }
        page->addKnob(param);
        param->setSecret(true);
        param->setTextToFitHorizontally("Color.alpha");
        _imp->alphaChannelKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamDisplayChannelsLabel) );
        param->setName(kViewerNodeParamDisplayChannels);
        param->setHintToolTip(tr(kViewerNodeParamDisplayChannelsHint));
        {
            std::vector<std::string> entries;
            entries.push_back("Luminance");
            entries.push_back("RGB");
            entries.push_back("Red");
            entries.push_back("Green");
            entries.push_back("Blue");
            entries.push_back("Alpha");
            entries.push_back("Matte");
            param->populateChoices(entries);
        }
        {
            std::map<int, std::string> shortcuts;
            shortcuts[0] = kViewerNodeParamActionLuminance;
            shortcuts[2] = kViewerNodeParamActionRed;
            shortcuts[3] = kViewerNodeParamActionGreen;
            shortcuts[4] = kViewerNodeParamActionBlue;
            shortcuts[5] = kViewerNodeParamActionAlpha;
            shortcuts[6] = kViewerNodeParamActionMatte;
            param->setShortcuts(shortcuts);
        }
        param->setTextToFitHorizontally("Luminance");
        page->addKnob(param);
        param->setDefaultValue(1);
        param->setIsDisplayChannelsKnob(true);
        param->setSecret(true);
        _imp->displayChannelsKnob[0] = param;
    }
    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamDisplayChannelsLabel) );
        param->setName(kViewerNodeParamDisplayChannelsB);
        {
            std::vector<std::string> entries;
            entries.push_back("Luminance");
            entries.push_back("RGB");
            entries.push_back("Red");
            entries.push_back("Green");
            entries.push_back("Blue");
            entries.push_back("Alpha");
            entries.push_back("Matte");
            param->populateChoices(entries);
        }
        param->setDefaultValue(1);
        page->addKnob(param);
        param->setSecret(true);
        _imp->displayChannelsKnob[1] = param;
    }


    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamZoomLabel) );
        param->setName(kViewerNodeParamZoom);
        param->setHintToolTip(tr(kViewerNodeParamZoomHint));
        param->setSecret(true);
        param->setMissingEntryWarningEnabled(false);
        param->setIsPersistent(false);
        {
            std::vector<std::string> entries;
            entries.push_back("Fit");
            entries.push_back("+");
            entries.push_back("-");
            entries.push_back("10%");
            entries.push_back("25%");
            entries.push_back("50%");
            entries.push_back("75%");
            entries.push_back("100%");
            entries.push_back("125%");
            entries.push_back("150%");
            entries.push_back("200%");
            entries.push_back("400%");
            entries.push_back("800%");
            entries.push_back("1600%");
            entries.push_back("2400%");
            entries.push_back("3200%");
            entries.push_back("6400%");
            param->populateChoices(entries);
        }
        {
            std::map<int, std::string> shortcuts;
            shortcuts[0] = kViewerNodeParamFitViewport;
            shortcuts[1] = kViewerNodeParamActionZoomIn;
            shortcuts[2] = kViewerNodeParamActionZoomOut;
            shortcuts[7] = kViewerNodeParamActionScaleOne;
            param->setShortcuts(shortcuts);
        }
        {
            std::vector<int> separators;
            separators.push_back(2);
            param->setSeparators(separators);
        }
        param->setTextToFitHorizontally("100000%");
        page->addKnob(param);
        _imp->zoomChoiceKnob = param;
    }


    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamClipToFormatLabel) );
        param->setName(kViewerNodeParamClipToFormat);
        param->setHintToolTip(tr(kViewerNodeParamClipToFormatHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setDefaultValue(true);
        param->setIconLabel(NATRON_IMAGES_PATH "cliptoprojectEnabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "cliptoprojectDisable.png", false);
        _imp->clipToFormatButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamFullFrameLabel) );
        param->setName(kViewerNodeParamFullFrame);
        param->setHintToolTip(tr(kViewerNodeParamFullFrameHint));
        param->setCheckable(true);
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "fullFrameOn.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "fullFrameOff.png", false);
        _imp->fullFrameButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableUserRoILabel) );
        param->setName(kViewerNodeParamEnableUserRoI);
        param->setHintToolTip(tr(kViewerNodeParamEnableUserRoIHint));
        param->addInViewerContextShortcutsReference(kViewerNodeParamActionCreateNewRoI);
        param->setCheckable(true);
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "viewer_roiEnabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "viewer_roiDisabled.png", false);
        addOverlaySlaveParam(param);
        _imp->toggleUserRoIButtonKnob = param;
    }

    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, std::string(kViewerNodeParamUserRoIBottomLeft), 2 );
        param->setDefaultValuesAreNormalized(true);
        param->setSecret(true);
        param->setDefaultValue(0.2, 0);
        param->setDefaultValue(0.2, 1);
        page->addKnob(param);
        _imp->userRoIBtmLeftKnob = param;
    }
    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, std::string(kViewerNodeParamUserRoISize), 2 );
        param->setDefaultValuesAreNormalized(true);
        param->setDefaultValue(.6, 0);
        param->setDefaultValue(.6, 1);
        param->setSecret(true);
        page->addKnob(param);
        _imp->userRoISizeKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableProxyModeLabel) );
        param->setName(kViewerNodeParamEnableProxyMode);
        param->setHintToolTip(tr(kViewerNodeParamEnableProxyModeHint));
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "renderScale_checked.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "renderScale.png", false);
        page->addKnob(param);
        _imp->toggleProxyModeButtonKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamProxyLevelLabel) );
        param->setName(kViewerNodeParamProxyLevel);
        param->setHintToolTip(tr(kViewerNodeParamProxyLevelHint));
        {
            std::vector<std::string> entries;
            entries.push_back("2");
            entries.push_back("4");
            entries.push_back("8");
            entries.push_back("16");
            entries.push_back("32");
            param->populateChoices(entries);
        }
        {
            std::map<int, std::string> shortcuts;
            shortcuts[0] = kViewerNodeParamActionProxy2;
            shortcuts[1] = kViewerNodeParamActionProxy4;
            shortcuts[2] = kViewerNodeParamActionProxy8;
            shortcuts[3] = kViewerNodeParamActionProxy16;
            shortcuts[4] = kViewerNodeParamActionProxy32;
            param->setShortcuts(shortcuts);
        }
        page->addKnob(param);
        param->setSecret(true);
        _imp->proxyChoiceKnob = param;
    }


    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPauseRenderLabel) );
        param->setName(kViewerNodeParamPauseRender);
        param->setHintToolTip(tr(kViewerNodeParamPauseRenderHint));
        param->addInViewerContextShortcutsReference(kViewerNodeParamActionPauseAB);
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setDefaultValue(false);
        param->setIconLabel(NATRON_IMAGES_PATH "pauseEnabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "pauseDisabled.png", false);
        _imp->pauseButtonKnob[0] = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPauseRenderLabel) );
        param->setName(kViewerNodeParamPauseRenderB);
        param->setHintToolTip(tr(kViewerNodeParamPauseRenderHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setDefaultValue(false);
        param->setIconLabel(NATRON_IMAGES_PATH "pauseEnabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "pauseDisabled.png", false);
        _imp->pauseButtonKnob[1] = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamAInputLabel) );
        param->setName(kViewerNodeParamAInput);
        param->setHintToolTip(tr(kViewerNodeParamAInputHint));
        param->setInViewerContextLabel(tr(kViewerNodeParamAInputLabel));
        {
            std::vector<std::string> entries;
            entries.push_back("-");
            param->populateChoices(entries);
        }
        page->addKnob(param);
        param->setTextToFitHorizontally("ColorCorrect1");
        param->setSecret(true);
        _imp->aInputNodeChoiceKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamOperationLabel) );
        param->setName(kViewerNodeParamOperation);
        param->setHintToolTip(tr(kViewerNodeParamOperation));
        {
            std::vector<std::string> entries, helps;
            entries.push_back("-");
            helps.push_back("");
            entries.push_back(kViewerNodeParamOperationWipeUnder);
            helps.push_back(kViewerNodeParamOperationWipeUnderHint);
            entries.push_back(kViewerNodeParamOperationWipeOver);
            helps.push_back(kViewerNodeParamOperationWipeOverHint);
            entries.push_back(kViewerNodeParamOperationWipeMinus);
            helps.push_back(kViewerNodeParamOperationWipeMinusHint);
            entries.push_back(kViewerNodeParamOperationWipeOnionSkin);
            helps.push_back(kViewerNodeParamOperationWipeOnionSkinHint);

            entries.push_back(kViewerNodeParamOperationStackUnder);
            helps.push_back(kViewerNodeParamOperationStackUnderHint);
            entries.push_back(kViewerNodeParamOperationStackOver);
            helps.push_back(kViewerNodeParamOperationStackOverHint);
            entries.push_back(kViewerNodeParamOperationStackMinus);
            helps.push_back(kViewerNodeParamOperationStackMinusHint);
            entries.push_back(kViewerNodeParamOperationStackOnionSkin);
            helps.push_back(kViewerNodeParamOperationStackOnionSkinHint);
            param->populateChoices(entries, helps);
        }
        {
            std::map<int, std::string> shortcuts;
            shortcuts[1] = kViewerNodeParamRightClickMenuToggleWipe;
            param->setShortcuts(shortcuts);
        }
        {
            std::vector<int> separators;
            separators.push_back(4);
            param->setSeparators(separators);
        }
        page->addKnob(param);
        param->setSecret(true);
        param->setTextToFitHorizontally("Wipe OnionSkin");
        param->setInViewerContextIconFilePath(NATRON_IMAGES_PATH "GroupingIcons/Set3/merge_grouping_3.png");
        _imp->blendingModeChoiceKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamBInputLabel) );
        param->setName(kViewerNodeParamBInput);
        param->setHintToolTip(tr(kViewerNodeParamBInputHint));
        param->setInViewerContextLabel(tr(kViewerNodeParamBInputLabel));
        {
            std::vector<std::string> entries;
            entries.push_back("-");
            param->populateChoices(entries);
        }
        param->setTextToFitHorizontally("ColorCorrect1");
        page->addKnob(param);
        param->setSecret(true);
        _imp->bInputNodeChoiceKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableGainLabel) );
        param->setName(kViewerNodeParamEnableGain);
        param->setHintToolTip(tr(kViewerNodeParamEnableGainHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "expoON.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "expoOFF.png", false);
        _imp->enableGainButtonKnob = param;
    }

    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, tr(kViewerNodeParamGainLabel), 1 );
        param->setName(kViewerNodeParamGain);
        param->setHintToolTip(tr(kViewerNodeParamGainHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setDisplayMinimum(-6.);
        param->setDisplayMaximum(6.);
        _imp->gainSliderKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableAutoContrastLabel) );
        param->setName(kViewerNodeParamEnableAutoContrast);
        param->setHintToolTip(tr(kViewerNodeParamEnableAutoContrastHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "AutoContrastON.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "AutoContrast.png", false);
        _imp->enableAutoContrastButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableGammaLabel) );
        param->setName(kViewerNodeParamEnableGamma);
        param->setHintToolTip(tr(kViewerNodeParamEnableGammaHint));
        page->addKnob(param);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "gammaON.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "gammaOFF.png", false);
        _imp->enableGammaButtonKnob = param;
    }

    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, tr(kViewerNodeParamGammaLabel), 1 );
        param->setName(kViewerNodeParamGamma);
        param->setHintToolTip(tr(kViewerNodeParamGammaHint));
        param->setDefaultValue(1.);
        page->addKnob(param);
        param->setSecret(true);
        param->setDisplayMinimum(0.);
        param->setDisplayMaximum(5.);
        _imp->gammaSliderKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamColorspaceLabel) );
        param->setName(kViewerNodeParamColorspace);
        param->setHintToolTip(tr(kViewerNodeParamColorspaceHint));
        {
            std::vector<std::string> entries;
            entries.push_back("Linear(None)");
            entries.push_back("sRGB");
            entries.push_back("Rec.709");
            param->populateChoices(entries);
        }
        param->setDefaultValue(1);
        page->addKnob(param);
        param->setSecret(true);
        _imp->colorspaceKnob = param;
    }

    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamViewLabel) );
        param->setName(kViewerNodeParamView);
        param->setHintToolTip(tr(kViewerNodeParamViewHint));
        {
            // Views gets populated in getPreferredMetadata
            std::vector<std::string> entries;
            param->populateChoices(entries);
        }
        page->addKnob(param);

        param->setSecret(true);
        _imp->activeViewKnob = param;
        refreshViewsKnobVisibility();
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRefreshViewportLabel) );
        param->setName(kViewerNodeParamRefreshViewport);
        param->setHintToolTip(tr(kViewerNodeParamRefreshViewportHint));
        param->addInViewerContextShortcutsReference(kViewerNodeParamActionRefreshWithStats);
        param->setSecret(true);
        param->setInViewerContextCanHaveShortcut(true);
        // Do not set evaluate on change, trigger the render ourselves in knobChance
        // We do this so that we can set down/up the button during render to give feedback to the user without triggering a new render
        param->setEvaluateOnChange(false);
        param->setIsPersistent(false);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "refreshActive.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "refresh.png", false);
        page->addKnob(param);
        _imp->refreshButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamSyncViewportsLabel) );
        param->setName(kViewerNodeParamSyncViewports);
        param->setHintToolTip(tr(kViewerNodeParamSyncViewportsHint));
        param->setInViewerContextCanHaveShortcut(true);
        param->setSecret(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "locked.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "unlocked.png", false);
        page->addKnob(param);
        _imp->syncViewersButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamFitViewportLabel) );
        param->setName(kViewerNodeParamFitViewport);
        param->setHintToolTip(tr(kViewerNodeParamFitViewportHint));
        param->setInViewerContextCanHaveShortcut(true);
        param->setSecret(true);
        param->setIconLabel(NATRON_IMAGES_PATH "centerViewer.png", true);
        page->addKnob(param);
        _imp->centerViewerButtonKnob = param;
    }


    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamCheckerBoardLabel) );
        param->setName(kViewerNodeParamCheckerBoard);
        param->setHintToolTip(tr(kViewerNodeParamCheckerBoardHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "checkerboard_on.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "checkerboard_off.png", false);
        page->addKnob(param);
        _imp->enableCheckerboardButtonKnob = param;
    }


    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableColorPickerLabel) );
        param->setName(kViewerNodeParamEnableColorPicker);
        param->setHintToolTip(tr(kViewerNodeParamEnableColorPickerHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setDefaultValue(true);
        param->setIconLabel(NATRON_IMAGES_PATH "color_picker.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "color_picker.png", false);
        page->addKnob(param);
        _imp->enableInfoBarButtonKnob = param;
    }

    // Player toolbar
    double projectFirst, projectLast;
    getApp()->getProject()->getFrameRange(&projectFirst, &projectLast);
    double projectFps = getApp()->getProject()->getProjectFrameRate();
    int currentFrame = getApp()->getTimeLine()->currentFrame();



    KnobPagePtr playerToolbarPage = AppManager::createKnob<KnobPage>( thisShared, tr("PlayerPage") );
    playerToolbarPage->setName(kViewerNodeParamPlayerToolBarPage);
    playerToolbarPage->setSecret(true);
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamSetInPointLabel) );
        param->setName(kViewerNodeParamSetInPoint);
        param->setHintToolTip(tr(kViewerNodeParamSetInPointHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "timelineIn.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "timelineIn.png", false);
        _imp->setInPointButtonKnob = param;
    }
    {
        KnobIntPtr param = AppManager::createKnob<KnobInt>( thisShared, tr(kViewerNodeParamInPointLabel) );
        param->setName(kViewerNodeParamInPoint);
        param->setHintToolTip(tr(kViewerNodeParamInPointHint));
        param->setSecret(true);
        param->setDefaultValue(projectFirst);
        param->setValueCenteredInSpinBox(true);
        param->setEvaluateOnChange(false);
        _imp->inPointKnob = param;
    }
    {
        KnobBoolPtr param = AppManager::createKnob<KnobBool>( thisShared, tr(kViewerNodeParamEnableFpsLabel) );
        param->setName(kViewerNodeParamEnableFps);
        param->setHintToolTip(tr(kViewerNodeParamEnableFpsHint));
        param->setInViewerContextLabel(tr("FPS"));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        _imp->enableFpsKnob = param;
    }

    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, tr(kViewerNodeParamFpsLabel) );
        param->setName(kViewerNodeParamFps);
        param->setHintToolTip(tr(kViewerNodeParamFpsHint));
        param->setSecret(true);
        param->setDefaultValue(projectFps);
        param->setEvaluateOnChange(false);
        param->setEnabled(0, false);
        param->setMinimum(0.);
        param->disableSlider();
        _imp->fpsKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamEnableTurboModeLabel) );
        param->setName(kViewerNodeParamEnableTurboMode);
        param->setHintToolTip(tr(kViewerNodeParamEnableTurboModeHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "turbo_on.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "turbo_off.png", false);
        _imp->enableTurboModeButtonKnob = param;
    }
    {
        KnobChoicePtr param = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamPlaybackModeLabel) );
        param->setName(kViewerNodeParamPlaybackMode);
        param->setHintToolTip(tr(kViewerNodeParamPlaybackModeHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        {
            std::vector<std::string> entries, helps;
            std::map<int ,std::string> icons;
            entries.push_back("Repeat");
            helps.push_back("Playback will loop over the timeline in/out points");
            icons[0] = NATRON_IMAGES_PATH "loopmode.png";
            entries.push_back("Bounce");
            helps.push_back("Playback will bounce between the timeline in/out points");
            icons[1] = NATRON_IMAGES_PATH "bounce.png";
            entries.push_back("Stop");
            helps.push_back("Playback will play once until reaches either the timeline's in or out point");
            icons[2] = NATRON_IMAGES_PATH "playOnce.png";
            param->populateChoices(entries, helps);
            param->setIcons(icons);
        }
        _imp->playbackModeKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamSyncTimelinesLabel) );
        param->setName(kViewerNodeParamSyncTimelines);
        param->setHintToolTip(tr(kViewerNodeParamSyncTimelinesHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setCheckable(true);
        param->setIconLabel(NATRON_IMAGES_PATH "locked.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "unlocked.png", false);
        _imp->syncTimelinesButtonKnob = param;
    }

    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamFirstFrameLabel) );
        param->setName(kViewerNodeParamFirstFrame);
        param->setHintToolTip(tr(kViewerNodeParamFirstFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "firstFrame.png", true);
        _imp->firstFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPlayBackwardLabel) );
        param->setName(kViewerNodeParamPlayBackward);
        param->setHintToolTip(tr(kViewerNodeParamPlayBackwardHint));
        param->addInViewerContextShortcutsReference(kViewerNodeParamActionAbortRender);
        param->setSecret(true);
        param->setCheckable(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "rewind_enabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "rewind.png", false);
        _imp->playBackwardButtonKnob = param;
    }
    {
        KnobIntPtr param = AppManager::createKnob<KnobInt>( thisShared, tr(kViewerNodeParamCurrentFrameLabel) );
        param->setName(kViewerNodeParamCurrentFrame);
        param->setHintToolTip(tr(kViewerNodeParamCurrentFrameHint));
        param->setSecret(true);
        param->setDefaultValue(currentFrame);
        param->setEvaluateOnChange(false);
        param->setIsPersistent(false);
        param->setValueCenteredInSpinBox(true);
        param->disableSlider();
        _imp->curFrameKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPlayForwardLabel) );
        param->setName(kViewerNodeParamPlayForward);
        param->setHintToolTip(tr(kViewerNodeParamPlayForwardHint));
        param->addInViewerContextShortcutsReference(kViewerNodeParamActionAbortRender);
        param->setSecret(true);
        param->setCheckable(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "play_enabled.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "play.png", false);
        _imp->playForwardButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamLastFrameLabel) );
        param->setName(kViewerNodeParamLastFrame);
        param->setHintToolTip(tr(kViewerNodeParamLastFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "lastFrame.png", true);
        _imp->lastFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPreviousFrameLabel) );
        param->setName(kViewerNodeParamPreviousFrame);
        param->setHintToolTip(tr(kViewerNodeParamPreviousFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "back1.png", true);
        _imp->prevFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamNextFrameLabel) );
        param->setName(kViewerNodeParamNextFrame);
        param->setHintToolTip(tr(kViewerNodeParamNextFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "forward1.png", true);
        _imp->nextFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPreviousKeyFrameLabel) );
        param->setName(kViewerNodeParamPreviousKeyFrame);
        param->setHintToolTip(tr(kViewerNodeParamPreviousKeyFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "prevKF.png", true);
        _imp->prevKeyFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamNextKeyFrameLabel) );
        param->setName(kViewerNodeParamNextKeyFrame);
        param->setHintToolTip(tr(kViewerNodeParamNextKeyFrameHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "nextKF.png", true);
        _imp->nextKeyFrameButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamPreviousIncrLabel) );
        param->setName(kViewerNodeParamPreviousIncr);
        param->setHintToolTip(tr(kViewerNodeParamPreviousIncrHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "previousIncr.png", true);
        _imp->prevIncrButtonKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamNextIncrLabel) );
        param->setName(kViewerNodeParamNextIncr);
        param->setHintToolTip(tr(kViewerNodeParamNextIncrHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "nextIncr.png", true);
        _imp->nextIncrButtonKnob = param;
    }
    {
        KnobIntPtr param = AppManager::createKnob<KnobInt>( thisShared, tr(kViewerNodeParamFrameIncrementLabel) );
        param->setName(kViewerNodeParamFrameIncrement);
        param->setHintToolTip(tr(kViewerNodeParamFrameIncrementHint));
        param->setSecret(true);
        param->setDefaultValue(10);
        param->setValueCenteredInSpinBox(true);
        param->setEvaluateOnChange(false);
        param->disableSlider();
        _imp->incrFrameKnob = param;
    }
    {
        KnobButtonPtr param = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamSetOutPointLabel) );
        param->setName(kViewerNodeParamSetOutPoint);
        param->setHintToolTip(tr(kViewerNodeParamSetOutPointHint));
        param->setSecret(true);
        param->setEvaluateOnChange(false);
        param->setInViewerContextCanHaveShortcut(true);
        param->setIconLabel(NATRON_IMAGES_PATH "timelineOut.png", true);
        param->setIconLabel(NATRON_IMAGES_PATH "timelineOut.png", false);
        _imp->setOutPointButtonKnob = param;
    }
    {
        KnobIntPtr param = AppManager::createKnob<KnobInt>( thisShared, tr(kViewerNodeParamOutPointLabel) );
        param->setName(kViewerNodeParamOutPoint);
        param->setHintToolTip(tr(kViewerNodeParamOutPointHint));
        param->setSecret(true);
        param->setValueCenteredInSpinBox(true);
        param->setDefaultValue(projectLast);
        param->setEvaluateOnChange(false);
        _imp->outPointKnob = param;
    }

    addKnobToViewerUI(_imp->layersKnob.lock());
    addKnobToViewerUI(_imp->alphaChannelKnob.lock());
    addKnobToViewerUI(_imp->displayChannelsKnob[0].lock());
    _imp->displayChannelsKnob[0].lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);
    addKnobToViewerUI(_imp->aInputNodeChoiceKnob.lock());
    addKnobToViewerUI(_imp->blendingModeChoiceKnob.lock());
    addKnobToViewerUI(_imp->bInputNodeChoiceKnob.lock());
    _imp->bInputNodeChoiceKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);

    addKnobToViewerUI(_imp->clipToFormatButtonKnob.lock());
    _imp->clipToFormatButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->toggleProxyModeButtonKnob.lock());
    _imp->toggleProxyModeButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->proxyChoiceKnob.lock());
    _imp->proxyChoiceKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->fullFrameButtonKnob.lock());
    _imp->fullFrameButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->toggleUserRoIButtonKnob.lock());
    _imp->toggleUserRoIButtonKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeSeparator);
    addKnobToViewerUI(_imp->refreshButtonKnob.lock());
    _imp->refreshButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->pauseButtonKnob[0].lock());
    _imp->pauseButtonKnob[0].lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeSeparator);

    addKnobToViewerUI(_imp->centerViewerButtonKnob.lock());
    _imp->centerViewerButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->syncViewersButtonKnob.lock());
    _imp->syncViewersButtonKnob.lock()->setInViewerContextItemSpacing(0);
    addKnobToViewerUI(_imp->zoomChoiceKnob.lock());
    _imp->zoomChoiceKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeAddNewLine);


    addKnobToViewerUI(_imp->enableGainButtonKnob.lock());
    addKnobToViewerUI(_imp->gainSliderKnob.lock());
    addKnobToViewerUI(_imp->enableAutoContrastButtonKnob.lock());
    addKnobToViewerUI(_imp->enableGammaButtonKnob.lock());
    addKnobToViewerUI(_imp->gammaSliderKnob.lock());
    addKnobToViewerUI(_imp->colorspaceKnob.lock());
    addKnobToViewerUI(_imp->enableCheckerboardButtonKnob.lock());
    addKnobToViewerUI(_imp->activeViewKnob.lock());
    _imp->activeViewKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);
    addKnobToViewerUI(_imp->enableInfoBarButtonKnob.lock());
    _imp->enableInfoBarButtonKnob.lock()->setInViewerContextItemSpacing(0);


    // Setup player buttons layout but don't add them to the viewer UI, since it's kind of a specific case, we want the toolbar below
    // the viewer
    playerToolbarPage->addKnob(_imp->setInPointButtonKnob.lock());
    _imp->setInPointButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->inPointKnob.lock());
    _imp->inPointKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);
    playerToolbarPage->addKnob(_imp->enableFpsKnob.lock());
    _imp->enableFpsKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->fpsKnob.lock());
    _imp->fpsKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeSeparator);
    playerToolbarPage->addKnob(_imp->enableTurboModeButtonKnob.lock());
    _imp->enableTurboModeButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->playbackModeKnob.lock());
    _imp->playbackModeKnob.lock()->setInViewerContextItemSpacing(VIEWER_UI_SECTIONS_SPACING_PX);
    playerToolbarPage->addKnob(_imp->syncTimelinesButtonKnob.lock());
    _imp->syncTimelinesButtonKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);
    playerToolbarPage->addKnob(_imp->firstFrameButtonKnob.lock());
    _imp->firstFrameButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->playBackwardButtonKnob.lock());
    _imp->playBackwardButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->curFrameKnob.lock());
    _imp->curFrameKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->playForwardButtonKnob.lock());
    _imp->playForwardButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->lastFrameButtonKnob.lock());
    _imp->lastFrameButtonKnob.lock()->setInViewerContextItemSpacing(VIEWER_UI_SECTIONS_SPACING_PX);
    playerToolbarPage->addKnob(_imp->prevFrameButtonKnob.lock());
    _imp->prevFrameButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->nextFrameButtonKnob.lock());
    _imp->nextFrameButtonKnob.lock()->setInViewerContextItemSpacing(VIEWER_UI_SECTIONS_SPACING_PX);
    playerToolbarPage->addKnob(_imp->prevKeyFrameButtonKnob.lock());
    _imp->prevKeyFrameButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->nextKeyFrameButtonKnob.lock());
    _imp->nextKeyFrameButtonKnob.lock()->setInViewerContextItemSpacing(VIEWER_UI_SECTIONS_SPACING_PX);
    playerToolbarPage->addKnob(_imp->prevIncrButtonKnob.lock());
    _imp->prevIncrButtonKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->incrFrameKnob.lock());
    _imp->incrFrameKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->nextIncrButtonKnob.lock());
    _imp->nextIncrButtonKnob.lock()->setInViewerContextLayoutType(eViewerContextLayoutTypeStretchAfter);
    playerToolbarPage->addKnob(_imp->outPointKnob.lock());
    _imp->outPointKnob.lock()->setInViewerContextItemSpacing(0);
    playerToolbarPage->addKnob(_imp->setOutPointButtonKnob.lock());

    // Right click menu
    KnobChoicePtr rightClickMenu = AppManager::createKnob<KnobChoice>( thisShared, std::string(kViewerNodeParamRightClickMenu) );
    rightClickMenu->setSecret(true);
    rightClickMenu->setEvaluateOnChange(false);
    page->addKnob(rightClickMenu);
    _imp->rightClickMenu = rightClickMenu;
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuToggleWipeLabel) );
        action->setName(kViewerNodeParamRightClickMenuToggleWipe);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickToggleWipe = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuCenterWipeLabel) );
        action->setName(kViewerNodeParamRightClickMenuCenterWipe);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickCenterWipe = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuPreviousLayerLabel) );
        action->setName(kViewerNodeParamRightClickMenuPreviousLayer);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickPreviousLayer = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuNextLayerLabel) );
        action->setName(kViewerNodeParamRightClickMenuNextLayer);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickNextLayer = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuPreviousViewLabel) );
        action->setName(kViewerNodeParamRightClickMenuPreviousView);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickPreviousView = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuNextViewLabel) );
        action->setName(kViewerNodeParamRightClickMenuNextView);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickNextView = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuSwitchABLabel) );
        action->setName(kViewerNodeParamRightClickMenuSwitchAB);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightClickSwitchAB = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideOverlaysLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHideOverlays);
        action->setSecret(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setEvaluateOnChange(false);
        addOverlaySlaveParam(action);
        page->addKnob(action);
        _imp->rightClickShowHideOverlays = action;
    }

    KnobChoicePtr showHideSubMenu = AppManager::createKnob<KnobChoice>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideSubMenuLabel) );
    showHideSubMenu->setName(kViewerNodeParamRightClickMenuShowHideSubMenu);
    showHideSubMenu->setSecret(true);
    showHideSubMenu->setEvaluateOnChange(false);
    page->addKnob(showHideSubMenu);
    _imp->rightClickShowHideSubMenu = showHideSubMenu;

    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuHideAllLabel) );
        action->setName(kViewerNodeParamRightClickMenuHideAll);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        page->addKnob(action);
        _imp->rightClickHideAll = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuHideAllTopLabel) );
        action->setName(kViewerNodeParamRightClickMenuHideAllTop);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        page->addKnob(action);
        _imp->rightClickHideAllTop = action;
    }


    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuHideAllBottomLabel) );
        action->setName(kViewerNodeParamRightClickMenuHideAllBottom);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        page->addKnob(action);
        _imp->rightClickHideAllBottom = action;
    }


    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHidePlayerLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHidePlayer);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        page->addKnob(action);
        _imp->rightClickShowHidePlayer = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideTimelineLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHideTimeline);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        page->addKnob(action);
        _imp->rightClickShowHideTimeline = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideLeftToolbarLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHideLeftToolbar);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        page->addKnob(action);
        _imp->rightClickShowHideLeftToolbar = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideTopToolbarLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHideTopToolbar);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        page->addKnob(action);
        _imp->rightClickShowHideTopToolbar = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamRightClickMenuShowHideTabHeaderLabel) );
        action->setName(kViewerNodeParamRightClickMenuShowHideTabHeader);
        action->setSecret(true);
        action->setInViewerContextCanHaveShortcut(true);
        action->setCheckable(true);
        action->setDefaultValue(true);
        page->addKnob(action);
        _imp->rightClickShowHideTabHeader = action;
    }


    // Viewer actions
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionLuminanceLabel) );
        action->setName(kViewerNodeParamActionLuminance);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayLuminanceAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionLuminanceALabel) );
        action->setName(kViewerNodeParamActionLuminanceA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayLuminanceAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionRedLabel) );
        action->setName(kViewerNodeParamActionRed);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayRedAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionRedALabel) );
        action->setName(kViewerNodeParamActionRedA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayRedAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionGreenLabel) );
        action->setName(kViewerNodeParamActionGreen);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayGreenAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionGreenALabel) );
        action->setName(kViewerNodeParamActionGreenA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayGreenAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionBlueLabel) );
        action->setName(kViewerNodeParamActionBlue);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayBlueAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionBlueALabel) );
        action->setName(kViewerNodeParamActionBlueA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayBlueAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionAlphaLabel) );
        action->setName(kViewerNodeParamActionAlpha);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayAlphaAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionAlphaALabel) );
        action->setName(kViewerNodeParamActionAlphaA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayAlphaAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionMatteLabel) );
        action->setName(kViewerNodeParamActionMatte);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayMatteAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionMatteALabel) );
        action->setName(kViewerNodeParamActionMatteA);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->displayMatteAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionZoomInLabel) );
        action->setName(kViewerNodeParamActionZoomIn);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->zoomInAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionZoomOutLabel) );
        action->setName(kViewerNodeParamActionZoomOut);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->zoomOutAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionScaleOneLabel) );
        action->setName(kViewerNodeParamActionScaleOne);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->zoomScaleOneAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionProxy2Label) );
        action->setName(kViewerNodeParamActionProxy2);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->proxyLevelAction[0] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionProxy4Label) );
        action->setName(kViewerNodeParamActionProxy4);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->proxyLevelAction[1] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionProxy8Label) );
        action->setName(kViewerNodeParamActionProxy8);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->proxyLevelAction[2] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionProxy16Label) );
        action->setName(kViewerNodeParamActionProxy16);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->proxyLevelAction[3] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionProxy32Label) );
        action->setName(kViewerNodeParamActionProxy32);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->proxyLevelAction[4] = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionLeftViewLabel) );
        action->setName(kViewerNodeParamActionLeftView);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->leftViewAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionRightViewLabel) );
        action->setName(kViewerNodeParamActionRightView);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->rightViewAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionPauseABLabel) );
        action->setName(kViewerNodeParamActionPauseAB);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->pauseABAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionCreateNewRoILabel) );
        action->setName(kViewerNodeParamActionCreateNewRoI);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->createUserRoIAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionRefreshWithStatsLabel) );
        action->setName(kViewerNodeParamActionRefreshWithStats);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->enableStatsAction = action;
    }
    {
        KnobButtonPtr action = AppManager::createKnob<KnobButton>( thisShared, tr(kViewerNodeParamActionAbortRenderLabel) );
        action->setName(kViewerNodeParamActionAbortRender);
        action->setSecret(true);
        action->setEvaluateOnChange(false);
        action->setInViewerContextCanHaveShortcut(true);
        page->addKnob(action);
        _imp->abortRenderingAction = action;
    }

    // Viewer overlays
    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, std::string(kViewerNodeParamWipeCenter), 2 );
        param->setName(kViewerNodeParamWipeCenter);
        param->setSecret(true);
        param->setDefaultValuesAreNormalized(true);
        param->setDefaultValue(0.5, 0);
        param->setDefaultValue(0.5, 1);
        page->addKnob(param);
        addOverlaySlaveParam(param);
        _imp->wipeCenter = param;
    }
    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, std::string(kViewerNodeParamWipeAmount), 1 );
        param->setName(kViewerNodeParamWipeAmount);
        param->setSecret(true);
        param->setDefaultValue(1.);
        page->addKnob(param);
        addOverlaySlaveParam(param);
        _imp->wipeAmount = param;
    }
    {
        KnobDoublePtr param = AppManager::createKnob<KnobDouble>( thisShared, std::string(kViewerNodeParamWipeAngle), 1 );
        param->setName(kViewerNodeParamWipeAngle);
        param->setSecret(true);
        page->addKnob(param);
        addOverlaySlaveParam(param);
        _imp->wipeAngle = param;
    }
} // initializeKnobs

void
ViewerNodePrivate::showRightClickMenu()
{
    KnobChoicePtr menu = rightClickMenu.lock();
    std::vector<std::string> entries;
    entries.push_back(kViewerNodeParamRightClickMenuToggleWipe);
    entries.push_back(kViewerNodeParamRightClickMenuCenterWipe);
    entries.push_back(kViewerNodeParamFitViewport);
    entries.push_back(kViewerNodeParamActionScaleOne);
    entries.push_back(kViewerNodeParamActionZoomIn);
    entries.push_back(kViewerNodeParamActionZoomOut);
    entries.push_back(kViewerNodeParamRightClickMenuPreviousLayer);
    entries.push_back(kViewerNodeParamRightClickMenuNextLayer);
    entries.push_back(kViewerNodeParamRightClickMenuPreviousView);
    entries.push_back(kViewerNodeParamRightClickMenuNextView);
    entries.push_back(kViewerNodeParamRightClickMenuSwitchAB);
    entries.push_back(kViewerNodeParamRightClickMenuShowHideOverlays);
    entries.push_back(kViewerNodeParamRightClickMenuShowHideSubMenu);
    entries.push_back(kViewerNodeParamActionRefreshWithStats);

    KnobChoicePtr showHideMenu = rightClickShowHideSubMenu.lock();
    std::vector<std::string> showHideEntries;
    showHideEntries.push_back(kViewerNodeParamRightClickMenuHideAll);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuHideAllTop);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuHideAllBottom);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuShowHideTopToolbar);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuShowHideLeftToolbar);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuShowHidePlayer);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuShowHideTimeline);
    showHideEntries.push_back(kViewerNodeParamRightClickMenuShowHideTabHeader);
    showHideEntries.push_back(kViewerNodeParamEnableColorPicker);

    {
        std::vector<int> separators;
        separators.push_back(2);
        showHideMenu->setSeparators(separators);
    }

    showHideMenu->populateChoices(showHideEntries);
    menu->populateChoices(entries);

}

void
ViewerNode::refreshViewsKnobVisibility()
{
    KnobChoicePtr knob = _imp->activeViewKnob.lock();
    if (knob) {
        knob->setInViewerContextSecret(getApp()->getProject()->getProjectViewsCount() <= 1);
    }
}

void
ViewerNodePrivate::refreshInputChoiceMenu(int internalIndex, int groupInputIndex)
{
    KnobChoicePtr inputChoiceKnob = internalIndex == 0 ? aInputNodeChoiceKnob.lock() : bInputNodeChoiceKnob.lock();

    assert(groupInputIndex >= 0 && groupInputIndex < (int)viewerInputs.size());
    NodePtr realNodeGroupInput = viewerInputs[groupInputIndex].node.lock();
    int index = -1;
    if (realNodeGroupInput) {
        // THe group effectively has an input, find it in the menu entries of the choice
        std::vector<std::string> entries = inputChoiceKnob->getEntries_mt_safe();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i] == realNodeGroupInput->getLabel()) {
                index = i;
                break;
            }
        }
    } else {
        // The group doesn't have any input, set the choice menu to "-"
        index = -1;
    }
    if (index == -1) {
        index = 0;
    }
    inputChoiceKnob->setValueFromPlugin(index, ViewSpec::current(), 0);

}

void
ViewerNode::connectInputToIndex(int groupInputIndex, int internalInputIndex)
{

    // We want to connect the node upstream of the internal viewer process node (or this node if there's nothing else upstream)
    // to the appropriate GroupInput node inside the group
    ViewerInstancePtr internalViewer = getInternalViewerNode();
    NodePtr internalNodeToConnect = internalViewer->getInputRecursive(internalInputIndex);
    assert(internalNodeToConnect);

    std::vector<NodePtr> inputNodes;
    getInputs(&inputNodes, false);
    if (groupInputIndex >= (int)inputNodes.size() || groupInputIndex < 0) {
        // Invalid input index
        return;
    }

    // This is the GroupInput node inside the group to connect to
    NodePtr groupInput = inputNodes[groupInputIndex];

    // Update the input choice
    _imp->refreshInputChoiceMenu(internalInputIndex, groupInputIndex);


    // Connect the node recursive upstream of the internal viewer process to the corresponding GroupInput node
    if (internalNodeToConnect == internalViewer->getNode()) {
        internalNodeToConnect->disconnectInput(internalInputIndex);
        internalNodeToConnect->connectInput(groupInput, internalInputIndex);
    } else {
        int prefInput = internalNodeToConnect->getPreferredInputForConnection();
        if (prefInput == -1) {
            // Preferred input might be connected, disconnect it first
            prefInput = internalNodeToConnect->getPreferredInput();
            if (prefInput != -1) {
                internalNodeToConnect->disconnectInput(prefInput);
            }
            internalNodeToConnect->connectInput(groupInput, prefInput);
        }
    }
}

void
ViewerNode::setZoomComboBoxText(const std::string& text)
{
    _imp->zoomChoiceKnob.lock()->setActiveEntry(text);
}

bool
ViewerNode::isLeftToolbarVisible() const
{
    return _imp->rightClickShowHideLeftToolbar.lock()->getValue();
}

bool
ViewerNode::isTopToolbarVisible() const
{
    return _imp->rightClickShowHideTopToolbar.lock()->getValue();
}

bool
ViewerNode::isTimelineVisible() const
{
    return _imp->rightClickShowHideTimeline.lock()->getValue();
}

bool
ViewerNode::isPlayerVisible() const
{
    return _imp->rightClickShowHidePlayer.lock()->getValue();
}

bool
ViewerNode::isInfoBarVisible() const
{
    return _imp->enableInfoBarButtonKnob.lock()->getValue();
}

bool
ViewerNode::knobChanged(const KnobIPtr& k, ValueChangedReasonEnum reason,
                        ViewSpec /*view*/,
                        double /*time*/,
                        bool /*originatedFromMainThread*/)
{
    if (!k || reason == eValueChangedReasonRestoreDefault) {
        return false;
    }

    ViewerInstancePtr internalViewerNode = getInternalViewerNode();
    if (!internalViewerNode) {
        return false;
    }
    bool caught = true;
    if (k == _imp->alphaChannelKnob.lock() && reason != eValueChangedReasonPluginEdited) {

        int currentIndex = _imp->alphaChannelKnob.lock()->getValue();
        std::set<ImageComponents> components;
        internalViewerNode->getInputsComponentsAvailables(&components);

        int i = 1; // because of the "-" choice
        for (std::set<ImageComponents>::iterator it = components.begin(); it != components.end(); ++it) {
            const std::vector<std::string>& channels = it->getComponentsNames();
            if ( currentIndex >= ( (int)channels.size() + i ) ) {
                i += channels.size();
            } else {
                for (U32 j = 0; j < channels.size(); ++j, ++i) {
                    if (i == currentIndex) {

                        internalViewerNode->setAlphaChannel(*it, channels[j]);
                        return true;
                    }
                }
            }
        }

    } else if (k == _imp->layersKnob.lock() && reason != eValueChangedReasonPluginEdited) {

        int currentIndex = _imp->layersKnob.lock()->getValue();
        std::set<ImageComponents> components;
        internalViewerNode->getInputsComponentsAvailables(&components);

        if ( ( currentIndex >= (int)(components.size() + 1) ) || (currentIndex < 0) ) {
            return false;
        }
        int i = 1; // because of the "-" choice
        int chanCount = 1; // because of the "-" choice
        for (std::set<ImageComponents>::iterator it = components.begin(); it != components.end(); ++it, ++i) {
            chanCount += it->getComponentsNames().size();
            if (i == currentIndex) {

                internalViewerNode->setCurrentLayer(*it);


                // If it has an alpha channel, set it
                if (it->getComponentsNames().size() == 4) {

                    // Use setValueFromPlugin so we don't recurse
                    _imp->alphaChannelKnob.lock()->setValueFromPlugin(chanCount - 1, ViewSpec::current(), 0);

                    internalViewerNode->setAlphaChannel(*it, it->getComponentsNames()[3]);
                }

                return true;
            }
        }

        _imp->alphaChannelKnob.lock()->setValueFromPlugin(0, ViewSpec::current(), 0);
        internalViewerNode->setAlphaChannel(ImageComponents::getNoneComponents(), std::string());
        internalViewerNode->setCurrentLayer(ImageComponents::getNoneComponents());

    } else if (k == _imp->aInputNodeChoiceKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            refreshInputFromChoiceMenu(0);
        }
    } else if (k == _imp->bInputNodeChoiceKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            refreshInputFromChoiceMenu(1);
        }
    } else if (k == _imp->blendingModeChoiceKnob.lock()) {
        ViewerCompositingOperatorEnum op = (ViewerCompositingOperatorEnum)_imp->blendingModeChoiceKnob.lock()->getValue();
        _imp->uiContext->setInfoBarVisible(1, op != eViewerCompositingOperatorNone);
        _imp->bInputNodeChoiceKnob.lock()->setAllDimensionsEnabled(op != eViewerCompositingOperatorNone);
    } else if (k == _imp->zoomChoiceKnob.lock()) {
        std::string zoomChoice = _imp->zoomChoiceKnob.lock()->getActiveEntryText_mt_safe();
        if (zoomChoice == "Fit") {
            _imp->uiContext->fitImageToFormat();
        } else if (zoomChoice == "+") {
             _imp->scaleZoomFactor(1.1);
        } else if (zoomChoice == "-") {
             _imp->scaleZoomFactor(0.9);
        } else {
            QString str = QString::fromUtf8(zoomChoice.c_str());
            str = str.trimmed();
            str = str.mid(0, str.size() - 1);
            int zoomInteger = str.toInt();
            _imp->uiContext->zoomViewport(zoomInteger / 100.);
        }
    } else if (k == _imp->enableGainButtonKnob.lock() && reason == eValueChangedReasonUserEdited) {
        double value;
        bool down = _imp->enableGainButtonKnob.lock()->getValue();
        if (down) {
            value = _imp->lastFstopValue;
        } else {
            value = 0;
        }
        _imp->gainSliderKnob.lock()->setValue(value);
    } else if (k == _imp->gainSliderKnob.lock()) {
        KnobButtonPtr enableKnob = _imp->enableGainButtonKnob.lock();
        if (reason == eValueChangedReasonUserEdited) {
            enableKnob->setValue(true);
            _imp->lastFstopValue =  _imp->gainSliderKnob.lock()->getValue();
        }
    } else if (k == _imp->enableGammaButtonKnob.lock() && reason == eValueChangedReasonUserEdited) {
        double value;
        bool down = _imp->enableGammaButtonKnob.lock()->getValue();
        if (down) {
            value = _imp->lastGammaValue;
        } else {
            value = 1;
        }
        _imp->gammaSliderKnob.lock()->setValue(value);

    } else if (k == _imp->gammaSliderKnob.lock()) {
        KnobButtonPtr enableKnob = _imp->enableGammaButtonKnob.lock();
        if (reason == eValueChangedReasonUserEdited) {
            enableKnob->setValue(true);
            _imp->lastGammaValue =  _imp->gammaSliderKnob.lock()->getValue();
        }

        getInternalViewerNode()->fillGammaLut(_imp->lastGammaValue);

    } else if (k == _imp->enableAutoContrastButtonKnob.lock()) {
        bool enable = _imp->enableAutoContrastButtonKnob.lock()->getValue();
        _imp->enableGammaButtonKnob.lock()->setAllDimensionsEnabled(!enable);
        _imp->gammaSliderKnob.lock()->setAllDimensionsEnabled(!enable);
        _imp->enableGainButtonKnob.lock()->setAllDimensionsEnabled(!enable);
        _imp->gainSliderKnob.lock()->setAllDimensionsEnabled(!enable);
        
    } else if (k == _imp->refreshButtonKnob.lock() && reason == eValueChangedReasonUserEdited) {
        getApp()->checkAllReadersModificationDate(false);
        NodePtr viewerNode = _imp->internalViewerProcessNode.lock();
        ViewerInstancePtr instance = toViewerInstance(viewerNode->getEffectInstance());
        assert(instance);
        instance->forceFullComputationOnNextFrame();
        instance->renderCurrentFrame(true);
    } else if (k == _imp->syncViewersButtonKnob.lock()) {

        getApp()->setMasterSyncViewer(getNode());
        NodesList allNodes;
        getApp()->getProject()->getNodes_recursive(allNodes, true);
        double left, bottom, factor, par;
        _imp->uiContext->getProjection(&left, &bottom, &factor, &par);
        ViewerInstancePtr thisInstance = toViewerInstance(_imp->getInternalViewerNode()->getEffectInstance());
        for (NodesList::iterator it = allNodes.begin(); it != allNodes.end(); ++it) {
            ViewerInstancePtr instance = toViewerInstance((*it)->getEffectInstance());
            if (instance && instance != thisInstance) {
                 instance->getUiContext()->setProjection(left, bottom, factor, par);
                instance->renderCurrentFrame(true);
            }
        }
    } else if (k == _imp->centerViewerButtonKnob.lock()) {
        if (!getApp()->isDuringPainting()) {
            _imp->uiContext->fitImageToFormat();
        }
    } else if (k == _imp->enableInfoBarButtonKnob.lock()) {
        bool infoBarVisible = _imp->enableInfoBarButtonKnob.lock()->getValue();
        if (reason == eValueChangedReasonUserEdited) {
            NodesList allNodes;
            getApp()->getProject()->getNodes_recursive(allNodes, true);
            ViewerNodePtr thisInstance = shared_from_this();
            for (NodesList::iterator it = allNodes.begin(); it != allNodes.end(); ++it) {
                ViewerNodePtr instance = toViewerNode((*it)->getEffectInstance());
                if (instance) {
                    if (instance != thisInstance) {
                        instance->_imp->enableInfoBarButtonKnob.lock()->setValue(infoBarVisible);
                    }
                    instance->_imp->uiContext->setInfoBarVisible(infoBarVisible);
                }
            }
        } else {
            _imp->uiContext->setInfoBarVisible(infoBarVisible);
        }
    } else if (k == _imp->rightClickToggleWipe.lock()) {
        KnobChoicePtr wipeChoiceKnob = _imp->blendingModeChoiceKnob.lock();
        int value = wipeChoiceKnob->getValue();
        if (value != 0) {
            wipeChoiceKnob->setValue(0);
        } else {
            if (_imp->lastWipeIndex == 0) {
                _imp->lastWipeIndex = 1;
            }
            wipeChoiceKnob->setValue(_imp->lastWipeIndex);
        }
    } else if (k == _imp->blendingModeChoiceKnob.lock() && reason == eValueChangedReasonUserEdited) {
        KnobChoicePtr wipeChoice = _imp->blendingModeChoiceKnob.lock();
        int value = wipeChoice->getValue();
        if (value != 0) {
            _imp->lastWipeIndex = value;
        }
    } else if (k == _imp->rightClickCenterWipe.lock()) {
        KnobDoublePtr knob = _imp->wipeCenter.lock();
        knob->setValues(_imp->lastMousePos.x(), _imp->lastMousePos.y(), ViewSpec::current(), eValueChangedReasonPluginEdited);
    } else if (k == _imp->rightClickNextLayer.lock()) {

    } else if (k == _imp->rightClickPreviousLayer.lock()) {

    } else if (k == _imp->rightClickSwitchAB.lock()) {
        NodePtr internalViewer = _imp->getInternalViewerNode();
        std::string aChoice = _imp->aInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe();
        std::string bChoice = _imp->bInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe();
        internalViewer->switchInput0And1();
        try {
            _imp->aInputNodeChoiceKnob.lock()->blockValueChanges();
            _imp->aInputNodeChoiceKnob.lock()->setValueFromLabel(bChoice, 0);
            _imp->aInputNodeChoiceKnob.lock()->unblockValueChanges();
            _imp->bInputNodeChoiceKnob.lock()->blockValueChanges();
            _imp->bInputNodeChoiceKnob.lock()->setValueFromLabel(aChoice, 0);
            _imp->bInputNodeChoiceKnob.lock()->unblockValueChanges();
        } catch (...) {

        }
    } else if (k == _imp->rightClickHideAll.lock()) {
        bool allHidden = _imp->rightClickHideAll.lock()->getValue();
        _imp->rightClickHideAllTop.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        _imp->rightClickHideAllBottom.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        if (reason != eValueChangedReasonPluginEdited) {
            if (!getApp()->isDuringPainting()) {
                _imp->uiContext->fitImageToFormat();
            }
        }
    } else if (k == _imp->rightClickHideAllTop.lock()) {
        bool allHidden = _imp->rightClickHideAllTop.lock()->getValue();
        _imp->rightClickShowHideTopToolbar.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        _imp->rightClickShowHideLeftToolbar.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        _imp->rightClickShowHideTabHeader.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        if (reason != eValueChangedReasonPluginEdited) {
            if (!getApp()->isDuringPainting()) {
                _imp->uiContext->fitImageToFormat();
            }
        }
    } else if (k == _imp->rightClickHideAllBottom.lock()) {
        bool allHidden = _imp->rightClickHideAllBottom.lock()->getValue();
        _imp->rightClickShowHidePlayer.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        _imp->rightClickShowHideTimeline.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        _imp->enableInfoBarButtonKnob.lock()->setValueFromPlugin(!allHidden, ViewSpec::current(), 0);
        if (reason != eValueChangedReasonPluginEdited) {
            if (!getApp()->isDuringPainting()) {
                _imp->uiContext->fitImageToFormat();
            }
        }
    } else if (k == _imp->rightClickShowHideTopToolbar.lock()) {
        bool topToolbarVisible = _imp->rightClickShowHideTopToolbar.lock()->getValue();
        _imp->uiContext->setTopToolBarVisible(topToolbarVisible);
    } else if (k == _imp->rightClickShowHideLeftToolbar.lock()) {
        bool visible = _imp->rightClickShowHideLeftToolbar.lock()->getValue();
        _imp->uiContext->setLeftToolBarVisible(visible);
    } else if (k == _imp->rightClickShowHidePlayer.lock()) {
        bool visible = _imp->rightClickShowHidePlayer.lock()->getValue();
        _imp->uiContext->setPlayerVisible(visible);
    } else if (k == _imp->rightClickShowHideTimeline.lock()) {
        bool visible = _imp->rightClickShowHideTimeline.lock()->getValue();
        _imp->uiContext->setTimelineVisible(visible);
    } else if (k == _imp->rightClickShowHideTabHeader.lock()) {
        bool visible = _imp->rightClickShowHideTabHeader.lock()->getValue();
        _imp->uiContext->setTabHeaderVisible(visible);
    } else if (k == _imp->displayRedAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsR) {
            setDisplayChannels((int)eDisplayChannelsR, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayRedAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsR) {
            setDisplayChannels((int)eDisplayChannelsR, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->displayGreenAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsG) {
            setDisplayChannels((int)eDisplayChannelsG, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayGreenAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsG) {
            setDisplayChannels((int)eDisplayChannelsG, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->displayBlueAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsB) {
            setDisplayChannels((int)eDisplayChannelsB, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayBlueAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsB) {
            setDisplayChannels((int)eDisplayChannelsB, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->displayAlphaAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsA) {
            setDisplayChannels((int)eDisplayChannelsA, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayAlphaAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsA) {
            setDisplayChannels((int)eDisplayChannelsA, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->displayMatteAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsMatte) {
            setDisplayChannels((int)eDisplayChannelsMatte, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayMatteAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsMatte) {
            setDisplayChannels((int)eDisplayChannelsMatte, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->displayLuminanceAction[0].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[0].lock()->getValue() != eDisplayChannelsY) {
            setDisplayChannels((int)eDisplayChannelsY, true);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, true);
        }
    } else if (k == _imp->displayLuminanceAction[1].lock()) {
        if ((DisplayChannelsEnum)_imp->displayChannelsKnob[1].lock()->getValue() != eDisplayChannelsY) {
            setDisplayChannels((int)eDisplayChannelsY, false);
        } else {
            setDisplayChannels((int)eDisplayChannelsRGB, false);
        }
    } else if (k == _imp->zoomInAction.lock()) {
        _imp->scaleZoomFactor(1.1);
    } else if (k == _imp->zoomOutAction.lock()) {
         _imp->scaleZoomFactor(0.9);
    } else if (k == _imp->zoomScaleOneAction.lock()) {
        _imp->uiContext->zoomViewport(1.);
    } else if (k == _imp->proxyLevelAction[0].lock()) {
        _imp->proxyChoiceKnob.lock()->setValue(0);
    } else if (k == _imp->proxyLevelAction[1].lock()) {
        _imp->proxyChoiceKnob.lock()->setValue(1);
    } else if (k == _imp->proxyLevelAction[2].lock()) {
        _imp->proxyChoiceKnob.lock()->setValue(2);
    } else if (k == _imp->proxyLevelAction[3].lock()) {
        _imp->proxyChoiceKnob.lock()->setValue(3);
    } else if (k == _imp->proxyLevelAction[4].lock()) {
        _imp->proxyChoiceKnob.lock()->setValue(4);
    } else if (k == _imp->leftViewAction.lock()) {
        _imp->activeViewKnob.lock()->setValue(0);
    } else if (k == _imp->rightViewAction.lock()) {
        const std::vector<std::string>& views = getApp()->getProject()->getProjectViewNames();
        if (views.size() > 1) {
            _imp->activeViewKnob.lock()->setValue(1);
        }
    } else if (k == _imp->pauseABAction.lock()) {
        bool curValue = _imp->pauseButtonKnob[0].lock()->getValue();
        _imp->pauseButtonKnob[0].lock()->setValue(!curValue);
        _imp->pauseButtonKnob[1].lock()->setValue(!curValue);
    } else if (k == _imp->createUserRoIAction.lock()) {
        _imp->buildUserRoIOnNextPress = true;
        _imp->toggleUserRoIButtonKnob.lock()->setValue(true);
        _imp->draggedUserRoI = getUserRoI();
    } else if (k == _imp->toggleUserRoIButtonKnob.lock()) {
        bool enabled = _imp->toggleUserRoIButtonKnob.lock()->getValue();
        if (!enabled) {
            _imp->buildUserRoIOnNextPress = false;
        }
    } else if (k == _imp->enableStatsAction.lock() && reason == eValueChangedReasonUserEdited) {
        getApp()->checkAllReadersModificationDate(false);
        NodePtr viewerNode = _imp->internalViewerProcessNode.lock();
        ViewerInstancePtr instance = toViewerInstance(viewerNode->getEffectInstance());
        assert(instance);
        instance->forceFullComputationOnNextFrame();
        getApp()->showRenderStatsWindow();
        instance->renderCurrentFrameWithRenderStats(true);
    } else if (k == _imp->rightClickPreviousLayer.lock()) {
        KnobChoicePtr knob = _imp->layersKnob.lock();
        int currentIndex = knob->getValue();
        int nChoices = knob->getNumEntries();

        if (currentIndex <= 1) {
            currentIndex = nChoices - 1;
        } else {
            --currentIndex;
        }
        if (currentIndex >= 0) {
            // User edited so the handler is executed
            knob->setValue(currentIndex, ViewSpec::current(), 0, eValueChangedReasonUserEdited, 0);
        }

    } else if (k == _imp->rightClickNextLayer.lock()) {

        KnobChoicePtr knob = _imp->layersKnob.lock();
        int currentIndex = knob->getValue();
        int nChoices = knob->getNumEntries();

        currentIndex = (currentIndex + 1) % nChoices;
        if ( (currentIndex == 0) && (nChoices > 1) ) {
            currentIndex = 1;
        }
        knob->setValue(currentIndex, ViewSpec::current(), 0, eValueChangedReasonUserEdited, 0);
    } else if (k == _imp->rightClickPreviousView.lock()) {
        KnobChoicePtr knob = _imp->activeViewKnob.lock();
        int currentIndex = knob->getValue();
        int nChoices = knob->getNumEntries();

        if (currentIndex == 0) {
            currentIndex = nChoices - 1;
        } else {
            --currentIndex;
        }
        if (currentIndex >= 0) {
            // User edited so the handler is executed
            knob->setValue(currentIndex, ViewSpec::current(), 0, eValueChangedReasonUserEdited, 0);
        }
    } else if (k == _imp->rightClickNextView.lock()) {
        KnobChoicePtr knob = _imp->activeViewKnob.lock();
        int currentIndex = knob->getValue();
        int nChoices = knob->getNumEntries();

        if (currentIndex == nChoices - 1) {
            currentIndex = 0;
        } else {
            currentIndex = currentIndex + 1;
        }

        knob->setValue(currentIndex, ViewSpec::current(), 0, eValueChangedReasonUserEdited, 0);
    } else if (k == _imp->enableFpsKnob.lock()) {
        _imp->fpsKnob.lock()->setAllDimensionsEnabled(_imp->enableFpsKnob.lock()->getValue());
        refreshFps();
    } else if (k == _imp->fpsKnob.lock()) {
        refreshFps();
    } else if (k == _imp->playForwardButtonKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            if (_imp->playForwardButtonKnob.lock()->getValue()) {
                _imp->startPlayback(eRenderDirectionForward);
            } else {
                _imp->abortAllViewersRendering();
            }
        }
    } else if (k == _imp->playBackwardButtonKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            if (_imp->playBackwardButtonKnob.lock()->getValue()) {
                _imp->startPlayback(eRenderDirectionBackward);
            } else {
                _imp->abortAllViewersRendering();
            }
        }
    } else if (k == _imp->curFrameKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            _imp->timelineGoTo(_imp->curFrameKnob.lock()->getValue());
        }
    } else if (k == _imp->prevFrameButtonKnob.lock()) {
        int prevFrame = getInternalViewerNode()->getTimeline()->currentFrame() - 1;

        if ( prevFrame  < _imp->inPointKnob.lock()->getValue() ) {
            prevFrame = _imp->outPointKnob.lock()->getValue();
        }
        _imp->timelineGoTo(prevFrame);
    } else if (k == _imp->nextFrameButtonKnob.lock()) {
        int nextFrame = getInternalViewerNode()->getTimeline()->currentFrame() + 1;

        if ( nextFrame > _imp->outPointKnob.lock()->getValue()) {
            nextFrame = _imp->inPointKnob.lock()->getValue();
        }
        _imp->timelineGoTo(nextFrame);
    } else if (k == _imp->prevKeyFrameButtonKnob.lock()) {
        getApp()->goToPreviousKeyframe();
    } else if (k == _imp->nextKeyFrameButtonKnob.lock()) {
        getApp()->goToNextKeyframe();
    } else if (k == _imp->prevIncrButtonKnob.lock()) {
        int time = getInternalViewerNode()->getTimeline()->currentFrame();
        time -= _imp->incrFrameKnob.lock()->getValue();
        _imp->timelineGoTo(time);
    } else if (k == _imp->nextIncrButtonKnob.lock()) {
        int time = getInternalViewerNode()->getTimeline()->currentFrame();
        time += _imp->incrFrameKnob.lock()->getValue();
        _imp->timelineGoTo(time);
    } else if (k == _imp->firstFrameButtonKnob.lock()) {
        int time = _imp->inPointKnob.lock()->getValue();
        _imp->timelineGoTo(time);
    } else if (k == _imp->lastFrameButtonKnob.lock()) {
        int time = _imp->outPointKnob.lock()->getValue();
        _imp->timelineGoTo(time);
    } else if (k == _imp->playbackModeKnob.lock()) {
        PlaybackModeEnum mode = (PlaybackModeEnum)_imp->playbackModeKnob.lock()->getValue();
        RenderEnginePtr engine = getInternalViewerNode()->getRenderEngine();
        if (engine) {
            engine->setPlaybackMode(mode);
        }
    } else if (k == _imp->syncTimelinesButtonKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            getUiContext()->setTripleSyncEnabled(_imp->syncTimelinesButtonKnob.lock()->getValue());
        }
    } else if (k == _imp->enableTurboModeButtonKnob.lock()) {
        if (reason != eValueChangedReasonPluginEdited) {
            getApp()->setGuiFrozen(_imp->enableTurboModeButtonKnob.lock()->getValue());
        }
    } else if (k == _imp->abortRenderingAction.lock()) {
        _imp->abortAllViewersRendering();
    } else if (k == _imp->setInPointButtonKnob.lock()) {
        _imp->inPointKnob.lock()->setValueFromPlugin(getInternalViewerNode()->getTimeline()->currentFrame(), ViewSpec::current(), 0);
    } else if (k == _imp->setOutPointButtonKnob.lock()) {
        _imp->outPointKnob.lock()->setValueFromPlugin(getInternalViewerNode()->getTimeline()->currentFrame(), ViewSpec::current(), 0);
    } else if (k == _imp->inPointKnob.lock()) {
        _imp->uiContext->setTimelineBounds(_imp->inPointKnob.lock()->getValue(), _imp->outPointKnob.lock()->getValue());
    } else if (k == _imp->outPointKnob.lock()) {
        _imp->uiContext->setTimelineBounds(_imp->inPointKnob.lock()->getValue(), _imp->outPointKnob.lock()->getValue());
    } else {
        caught = false;
    }
    return caught;

} // knobChanged

void
ViewerNode::setDisplayChannels(int index, bool setBoth)
{

    for (int i = 0; i < 2; ++i) {
        if (i == 1 && !setBoth) {
            break;
        }
        KnobChoicePtr displayChoice = _imp->displayChannelsKnob[i].lock();
        displayChoice->setValue(index);
    }
}
DisplayChannelsEnum
ViewerNode::getDisplayChannels(int index) const
{
    KnobChoicePtr displayChoice = _imp->displayChannelsKnob[index].lock();
    return (DisplayChannelsEnum)displayChoice->getValue();
}

bool
ViewerNode::isAutoContrastEnabled() const
{
    return _imp->enableAutoContrastButtonKnob.lock()->getValue();
}

ViewerColorSpaceEnum
ViewerNode::getColorspace() const
{
    return (ViewerColorSpaceEnum)_imp->colorspaceKnob.lock()->getValue();
}

void
ViewerNodePrivate::drawUserRoI()
{
    OverlaySupport* viewport = _publicInterface->getCurrentViewportForOverlays();
    Point pixelScale;
    viewport->getPixelScale(pixelScale.x, pixelScale.y);

    {
        GLProtectAttrib<GL_GPU> a(GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_ENABLE_BIT);

        GL_GPU::glDisable(GL_BLEND);

        GL_GPU::glColor4f(0.9, 0.9, 0.9, 1.);

        RectD userRoI;
        if ( uiState == eViewerNodeInteractMouseStateBuildingUserRoI ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiBottomEdge ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiBottomLeft ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiBottomRight ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiRightEdge ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiTopRight ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiTopEdge ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiTopLeft ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiLeftEdge ||
            uiState == eViewerNodeInteractMouseStateDraggingRoiCross ||
            buildUserRoIOnNextPress ) {
            userRoI = draggedUserRoI;
        } else {
            userRoI = _publicInterface->getUserRoI();
        }
        
        
        if (buildUserRoIOnNextPress) {
            GL_GPU::glLineStipple(2, 0xAAAA);
            GL_GPU::glEnable(GL_LINE_STIPPLE);
        }

        ///base rect
        GL_GPU::glBegin(GL_LINE_LOOP);
        GL_GPU::glVertex2f(userRoI.x1, userRoI.y1); //bottom left
        GL_GPU::glVertex2f(userRoI.x1, userRoI.y2); //top left
        GL_GPU::glVertex2f(userRoI.x2, userRoI.y2); //top right
        GL_GPU::glVertex2f(userRoI.x2, userRoI.y1); //bottom right
        GL_GPU::glEnd();


        GL_GPU::glBegin(GL_LINES);
        ///border ticks
        double borderTickWidth = USER_ROI_BORDER_TICK_SIZE * pixelScale.x;
        double borderTickHeight = USER_ROI_BORDER_TICK_SIZE * pixelScale.y;
        GL_GPU::glVertex2f(userRoI.x1, (userRoI.y1 + userRoI.y2) / 2);
        GL_GPU::glVertex2f(userRoI.x1 - borderTickWidth, (userRoI.y1 + userRoI.y2) / 2);

        GL_GPU::glVertex2f(userRoI.x2, (userRoI.y1 + userRoI.y2) / 2);
        GL_GPU::glVertex2f(userRoI.x2 + borderTickWidth, (userRoI.y1 + userRoI.y2) / 2);

        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, userRoI.y2 );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, userRoI.y2 + borderTickHeight );

        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, userRoI.y1 );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, userRoI.y1 - borderTickHeight );

        ///middle cross
        double crossWidth = USER_ROI_CROSS_RADIUS * pixelScale.x;
        double crossHeight = USER_ROI_CROSS_RADIUS * pixelScale.y;
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, (userRoI.y1 + userRoI.y2) / 2 - crossHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2, (userRoI.y1 + userRoI.y2) / 2 + crossHeight );

        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2  - crossWidth, (userRoI.y1 + userRoI.y2) / 2 );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2  + crossWidth, (userRoI.y1 + userRoI.y2) / 2 );
        GL_GPU::glEnd();


        ///draw handles hint for the user
        GL_GPU::glBegin(GL_QUADS);

        double rectHalfWidth = (USER_ROI_SELECTION_POINT_SIZE * pixelScale.x) / 2.;
        double rectHalfHeight = (USER_ROI_SELECTION_POINT_SIZE * pixelScale.y) / 2.;
        //left
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight);

        //top
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, userRoI.y2 - rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, userRoI.y2 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, userRoI.y2 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, userRoI.y2 - rectHalfHeight );

        //right
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight);

        //bottom
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, userRoI.y1 - rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, userRoI.y1 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, userRoI.y1 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, userRoI.y1 - rectHalfHeight );

        //middle
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 - rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 + rectHalfHeight );
        GL_GPU::glVertex2f( (userRoI.x1 +  userRoI.x2) / 2 + rectHalfWidth, (userRoI.y1 + userRoI.y2) / 2 - rectHalfHeight );


        //top left
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, userRoI.y2 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, userRoI.y2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, userRoI.y2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, userRoI.y2 - rectHalfHeight);

        //top right
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, userRoI.y2 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, userRoI.y2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, userRoI.y2 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, userRoI.y2 - rectHalfHeight);

        //bottom right
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, userRoI.y1 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 - rectHalfWidth, userRoI.y1 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, userRoI.y1 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x2 + rectHalfWidth, userRoI.y1 - rectHalfHeight);


        //bottom left
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, userRoI.y1 - rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 - rectHalfWidth, userRoI.y1 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, userRoI.y1 + rectHalfHeight);
        GL_GPU::glVertex2f(userRoI.x1 + rectHalfWidth, userRoI.y1 - rectHalfHeight);

        GL_GPU::glEnd();

        if (buildUserRoIOnNextPress) {
            GL_GPU::glDisable(GL_LINE_STIPPLE);
        }
    } // GLProtectAttrib a(GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_ENABLE_BIT);
} // drawUserRoI

void
ViewerNodePrivate::drawArcOfCircle(const QPointF & center,
                                   double radiusX,
                                   double radiusY,
                                   double startAngle,
                                   double endAngle)
{
    double alpha = startAngle;
    double x, y;

    {
        GLProtectAttrib<GL_GPU> a(GL_CURRENT_BIT);

        if ( (hoverState == eHoverStateWipeMix) || (uiState == eViewerNodeInteractMouseStateDraggingWipeMixHandle) ) {
            GL_GPU::glColor3f(0, 1, 0);
        }
        GL_GPU::glBegin(GL_POINTS);
        while (alpha <= endAngle) {
            x = center.x()  + radiusX * std::cos(alpha);
            y = center.y()  + radiusY * std::sin(alpha);
            GL_GPU::glVertex2d(x, y);
            alpha += 0.01;
        }
        GL_GPU::glEnd();
    } // GLProtectAttrib a(GL_CURRENT_BIT);
}

void
ViewerNodePrivate::drawWipeControl()
{
    OverlaySupport* viewport = _publicInterface->getCurrentViewportForOverlays();
    Point pixelScale;
    viewport->getPixelScale(pixelScale.x, pixelScale.y);

    double angle;
    QPointF center;
    double mixAmount;
    {
        angle = wipeAngle.lock()->getValue();
        KnobDoublePtr centerKnob = wipeCenter.lock();
        center.rx() = centerKnob->getValue();
        center.ry() = centerKnob->getValue(1);
        mixAmount = wipeAmount.lock()->getValue();
    }
    double alphaMix1, alphaMix0, alphaCurMix;

    alphaMix1 = angle + M_PI_4 / 2;
    alphaMix0 = angle + 3. * M_PI_4 / 2;
    alphaCurMix = mixAmount * (alphaMix1 - alphaMix0) + alphaMix0;
    QPointF mix0Pos, mixPos, mix1Pos;
    double mixX, mixY, rotateW, rotateH, rotateOffsetX, rotateOffsetY;

    mixX = WIPE_MIX_HANDLE_LENGTH * pixelScale.x;
    mixY = WIPE_MIX_HANDLE_LENGTH * pixelScale.y;
    rotateW = WIPE_ROTATE_HANDLE_LENGTH * pixelScale.x;
    rotateH = WIPE_ROTATE_HANDLE_LENGTH * pixelScale.y;
    rotateOffsetX = WIPE_ROTATE_OFFSET * pixelScale.x;
    rotateOffsetY = WIPE_ROTATE_OFFSET * pixelScale.y;


    mixPos.setX(center.x() + std::cos(alphaCurMix) * mixX);
    mixPos.setY(center.y() + std::sin(alphaCurMix) * mixY);
    mix0Pos.setX(center.x() + std::cos(alphaMix0) * mixX);
    mix0Pos.setY(center.y() + std::sin(alphaMix0) * mixY);
    mix1Pos.setX(center.x() + std::cos(alphaMix1) * mixX);
    mix1Pos.setY(center.y() + std::sin(alphaMix1) * mixY);

    QPointF oppositeAxisBottom, oppositeAxisTop, rotateAxisLeft, rotateAxisRight;
    rotateAxisRight.setX( center.x() + std::cos(angle) * (rotateW - rotateOffsetX) );
    rotateAxisRight.setY( center.y() + std::sin(angle) * (rotateH - rotateOffsetY) );
    rotateAxisLeft.setX(center.x() - std::cos(angle) * rotateOffsetX);
    rotateAxisLeft.setY( center.y() - (std::sin(angle) * rotateOffsetY) );

    oppositeAxisTop.setX( center.x() + std::cos(angle + M_PI_2) * (rotateW / 2.) );
    oppositeAxisTop.setY( center.y() + std::sin(angle + M_PI_2) * (rotateH / 2.) );
    oppositeAxisBottom.setX( center.x() - std::cos(angle + M_PI_2) * (rotateW / 2.) );
    oppositeAxisBottom.setY( center.y() - std::sin(angle + M_PI_2) * (rotateH / 2.) );

    {
        GLProtectAttrib<GL_GPU> a(GL_ENABLE_BIT | GL_LINE_BIT | GL_CURRENT_BIT | GL_HINT_BIT | GL_TRANSFORM_BIT | GL_COLOR_BUFFER_BIT);
        //GLProtectMatrix p(GL_PROJECTION); // useless (we do two glTranslate in opposite directions)

        // Draw everything twice
        // l = 0: shadow
        // l = 1: drawing
        double baseColor[3];
        for (int l = 0; l < 2; ++l) {
            // shadow (uses GL_PROJECTION)
            GL_GPU::glMatrixMode(GL_PROJECTION);
            int direction = (l == 0) ? 1 : -1;
            // translate (1,-1) pixels
            GL_GPU::glTranslated(direction * pixelScale.x, -direction * pixelScale.y, 0);
            GL_GPU::glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

            if (l == 0) {
                // Draw a shadow for the cross hair
                baseColor[0] = baseColor[1] = baseColor[2] = 0.;
            } else {
                baseColor[0] = baseColor[1] = baseColor[2] = 0.8;
            }

            GL_GPU::glEnable(GL_BLEND);
            GL_GPU::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            GL_GPU::glEnable(GL_LINE_SMOOTH);
            GL_GPU::glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
            GL_GPU::glLineWidth(1.5);
            GL_GPU::glBegin(GL_LINES);
            if ( (hoverState == eHoverStateWipeRotateHandle) || (uiState == eViewerNodeInteractMouseStateRotatingWipeHandle) ) {
                GL_GPU::glColor4f(0., 1. * l, 0., 1.);
            }
            GL_GPU::glColor4f(baseColor[0], baseColor[1], baseColor[2], 1.);
            GL_GPU::glVertex2d( rotateAxisLeft.x(), rotateAxisLeft.y() );
            GL_GPU::glVertex2d( rotateAxisRight.x(), rotateAxisRight.y() );
            GL_GPU::glVertex2d( oppositeAxisBottom.x(), oppositeAxisBottom.y() );
            GL_GPU::glVertex2d( oppositeAxisTop.x(), oppositeAxisTop.y() );
            GL_GPU::glVertex2d( center.x(), center.y() );
            GL_GPU::glVertex2d( mixPos.x(), mixPos.y() );
            GL_GPU::glEnd();
            GL_GPU::glLineWidth(1.);

            ///if hovering the rotate handle or dragging it show a small bended arrow
            if ( (hoverState == eHoverStateWipeRotateHandle) || (uiState == eViewerNodeInteractMouseStateRotatingWipeHandle) ) {
                GLProtectMatrix<GL_GPU> p(GL_MODELVIEW);

                GL_GPU::glColor4f(0., 1. * l, 0., 1.);
                double arrowCenterX = WIPE_ROTATE_HANDLE_LENGTH * pixelScale.x / 2;
                ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
                OfxPointD arrowRadius;
                arrowRadius.x = 5. * pixelScale.x;
                arrowRadius.y = 10. * pixelScale.y;

                GL_GPU::glTranslatef(center.x(), center.y(), 0.);
                GL_GPU::glRotatef(angle * 180.0 / M_PI, 0, 0, 1);
                //  center the oval at x_center, y_center
                GL_GPU::glTranslatef (arrowCenterX, 0., 0);
                //  draw the oval using line segments
                GL_GPU::glBegin (GL_LINE_STRIP);
                GL_GPU::glVertex2f (0, arrowRadius.y);
                GL_GPU::glVertex2f (arrowRadius.x, 0.);
                GL_GPU::glVertex2f (0, -arrowRadius.y);
                GL_GPU::glEnd ();


                GL_GPU::glBegin(GL_LINES);
                ///draw the top head
                GL_GPU::glVertex2f(0., arrowRadius.y);
                GL_GPU::glVertex2f(0., arrowRadius.y -  arrowRadius.x );

                GL_GPU::glVertex2f(0., arrowRadius.y);
                GL_GPU::glVertex2f(4. * pixelScale.x, arrowRadius.y - 3. * pixelScale.y); // 5^2 = 3^2+4^2

                ///draw the bottom head
                GL_GPU::glVertex2f(0., -arrowRadius.y);
                GL_GPU::glVertex2f(0., -arrowRadius.y + 5. * pixelScale.y);

                GL_GPU::glVertex2f(0., -arrowRadius.y);
                GL_GPU::glVertex2f(4. * pixelScale.x, -arrowRadius.y + 3. * pixelScale.y); // 5^2 = 3^2+4^2

                GL_GPU::glEnd();

                GL_GPU::glColor4f(baseColor[0], baseColor[1], baseColor[2], 1.);
            }

            GL_GPU::glPointSize(5.);
            GL_GPU::glEnable(GL_POINT_SMOOTH);
            GL_GPU::glBegin(GL_POINTS);
            GL_GPU::glVertex2d( center.x(), center.y() );
            if ( ( (hoverState == eHoverStateWipeMix) &&
                  (uiState != eViewerNodeInteractMouseStateRotatingWipeHandle) )
                || (uiState == eViewerNodeInteractMouseStateDraggingWipeMixHandle) ) {
                GL_GPU::glColor4f(0., 1. * l, 0., 1.);
            }
            GL_GPU::glVertex2d( mixPos.x(), mixPos.y() );
            GL_GPU::glEnd();
            GL_GPU::glPointSize(1.);
            
            drawArcOfCircle(center, mixX, mixY, angle + M_PI_4 / 2, angle + 3. * M_PI_4 / 2);
        }
    } // GLProtectAttrib a(GL_ENABLE_BIT | GL_LINE_BIT | GL_CURRENT_BIT | GL_HINT_BIT | GL_TRANSFORM_BIT | GL_COLOR_BUFFER_BIT);
} // drawWipeControl

void
ViewerNode::drawOverlay(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/)
{
    bool userRoIEnabled = _imp->toggleUserRoIButtonKnob.lock()->getValue();
    if (userRoIEnabled) {
        _imp->drawUserRoI();
    }

    ViewerCompositingOperatorEnum compOperator = (ViewerCompositingOperatorEnum)_imp->blendingModeChoiceKnob.lock()->getValue();
    if (compOperator != eViewerCompositingOperatorNone &&
        compOperator != eViewerCompositingOperatorStackUnder &&
        compOperator != eViewerCompositingOperatorStackOver &&
        compOperator != eViewerCompositingOperatorStackMinus &&
        compOperator != eViewerCompositingOperatorStackOnionSkin) {
        _imp->drawWipeControl();
    }


} // drawOverlay


bool
ViewerNodePrivate::isNearbyWipeCenter(const QPointF& wipeCenter,
                                      const QPointF & pos,
                                             double zoomScreenPixelWidth,
                                             double zoomScreenPixelHeight)
{
    double toleranceX = zoomScreenPixelWidth * 8.;
    double toleranceY = zoomScreenPixelHeight * 8.;

    if ( ( pos.x() >= (wipeCenter.x() - toleranceX) ) && ( pos.x() <= (wipeCenter.x() + toleranceX) ) &&
        ( pos.y() >= (wipeCenter.y() - toleranceY) ) && ( pos.y() <= (wipeCenter.y() + toleranceY) ) ) {
        return true;
    }

    return false;
}

bool
ViewerNodePrivate::isNearbyWipeRotateBar(const QPointF& wipeCenter,
                                         double wipeAngle,
                                         const QPointF & pos,
                                                double zoomScreenPixelWidth,
                                                double zoomScreenPixelHeight) 
{
    double toleranceX = zoomScreenPixelWidth * 8.;
    double toleranceY = zoomScreenPixelHeight * 8.;
    double rotateX, rotateY, rotateOffsetX, rotateOffsetY;

    rotateX = WIPE_ROTATE_HANDLE_LENGTH * zoomScreenPixelWidth;
    rotateY = WIPE_ROTATE_HANDLE_LENGTH * zoomScreenPixelHeight;
    rotateOffsetX = WIPE_ROTATE_OFFSET * zoomScreenPixelWidth;
    rotateOffsetY = WIPE_ROTATE_OFFSET * zoomScreenPixelHeight;

    QPointF outterPoint;

    outterPoint.setX( wipeCenter.x() + std::cos(wipeAngle) * (rotateX - rotateOffsetX) );
    outterPoint.setY( wipeCenter.y() + std::sin(wipeAngle) * (rotateY - rotateOffsetY) );
    if ( ( ( ( pos.y() >= (wipeCenter.y() - toleranceY) ) && ( pos.y() <= (outterPoint.y() + toleranceY) ) ) ||
          ( ( pos.y() >= (outterPoint.y() - toleranceY) ) && ( pos.y() <= (wipeCenter.y() + toleranceY) ) ) ) &&
        ( ( ( pos.x() >= (wipeCenter.x() - toleranceX) ) && ( pos.x() <= (outterPoint.x() + toleranceX) ) ) ||
         ( ( pos.x() >= (outterPoint.x() - toleranceX) ) && ( pos.x() <= (wipeCenter.x() + toleranceX) ) ) ) ) {
            Point a;
            a.x = ( outterPoint.x() - wipeCenter.x() );
            a.y = ( outterPoint.y() - wipeCenter.y() );
            double norm = sqrt(a.x * a.x + a.y * a.y);

            ///The point is in the bounding box of the segment, if it is vertical it must be on the segment anyway
            if (norm == 0) {
                return false;
            }

            a.x /= norm;
            a.y /= norm;
            Point b;
            b.x = ( pos.x() - wipeCenter.x() );
            b.y = ( pos.y() - wipeCenter.y() );
            norm = sqrt(b.x * b.x + b.y * b.y);

            ///This vector is not vertical
            if (norm != 0) {
                b.x /= norm;
                b.y /= norm;

                double crossProduct = b.y * a.x - b.x * a.y;
                if (std::abs(crossProduct) <  0.1) {
                    return true;
                }
            }
        }

    return false;
} // ViewerGL::Implementation::isNearbyWipeRotateBar

bool
ViewerNodePrivate::isNearbyWipeMixHandle(const QPointF& wipeCenter,
                                         double wipeAngle,
                                         double mixAmount,
                                         const QPointF & pos,
                                                double zoomScreenPixelWidth,
                                                double zoomScreenPixelHeight)
{
    double toleranceX = zoomScreenPixelWidth * 8.;
    double toleranceY = zoomScreenPixelHeight * 8.;
    ///mix 1 is at rotation bar + pi / 8
    ///mix 0 is at rotation bar + 3pi / 8
    double alphaMix1, alphaMix0, alphaCurMix;

    alphaMix1 = wipeAngle + M_PI_4 / 2;
    alphaMix0 = wipeAngle + 3 * M_PI_4 / 2;
    alphaCurMix = mixAmount * (alphaMix1 - alphaMix0) + alphaMix0;
    QPointF mixPos;
    double mixX = WIPE_MIX_HANDLE_LENGTH * zoomScreenPixelWidth;
    double mixY = WIPE_MIX_HANDLE_LENGTH * zoomScreenPixelHeight;

    mixPos.setX(wipeCenter.x() + std::cos(alphaCurMix) * mixX);
    mixPos.setY(wipeCenter.y() + std::sin(alphaCurMix) * mixY);
    if ( ( pos.x() >= (mixPos.x() - toleranceX) ) && ( pos.x() <= (mixPos.x() + toleranceX) ) &&
        ( pos.y() >= (mixPos.y() - toleranceY) ) && ( pos.y() <= (mixPos.y() + toleranceY) ) ) {
        return true;
    }
    
    return false;
}

bool
ViewerNodePrivate::isNearByUserRoITopEdge(const RectD & roi,
                                 const QPointF & zoomPos,
                                 double zoomScreenPixelWidth,
                                 double zoomScreenPixelHeight)
{
    double length = std::min(roi.x2 - roi.x1 - 10, (USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth) * 2);
    RectD r(roi.x1 + length / 2,
            roi.y2 - USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight,
            roi.x2 - length / 2,
            roi.y2 + USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight);

    return r.contains( zoomPos.x(), zoomPos.y() );
}

bool
ViewerNodePrivate::isNearByUserRoIRightEdge(const RectD & roi,
                                   const QPointF & zoomPos,
                                   double zoomScreenPixelWidth,
                                   double zoomScreenPixelHeight)
{
    double length = std::min(roi.y2 - roi.y1 - 10, (USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight) * 2);
    RectD r(roi.x2 - USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth,
            roi.y1 + length / 2,
            roi.x2 + USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth,
            roi.y2 - length / 2);

    return r.contains( zoomPos.x(), zoomPos.y() );
}

bool
ViewerNodePrivate::isNearByUserRoILeftEdge(const RectD & roi,
                                  const QPointF & zoomPos,
                                  double zoomScreenPixelWidth,
                                  double zoomScreenPixelHeight)
{
    double length = std::min(roi.y2 - roi.y1 - 10, (USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight) * 2);
    RectD r(roi.x1 - USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth,
            roi.y1 + length / 2,
            roi.x1 + USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth,
            roi.y2 - length / 2);

    return r.contains( zoomPos.x(), zoomPos.y() );
}

bool
ViewerNodePrivate::isNearByUserRoIBottomEdge(const RectD & roi,
                                    const QPointF & zoomPos,
                                    double zoomScreenPixelWidth,
                                    double zoomScreenPixelHeight)
{
    double length = std::min(roi.x2 - roi.x1 - 10, (USER_ROI_CLICK_TOLERANCE * zoomScreenPixelWidth) * 2);
    RectD r(roi.x1 + length / 2,
            roi.y1 - USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight,
            roi.x2 - length / 2,
            roi.y1 + USER_ROI_CLICK_TOLERANCE * zoomScreenPixelHeight);

    return r.contains( zoomPos.x(), zoomPos.y() );
}

bool
ViewerNodePrivate::isNearByUserRoI(double x,
                          double y,
                          const QPointF & zoomPos,
                          double zoomScreenPixelWidth,
                          double zoomScreenPixelHeight)
{
    RectD r(x - USER_ROI_CROSS_RADIUS * zoomScreenPixelWidth,
            y - USER_ROI_CROSS_RADIUS * zoomScreenPixelHeight,
            x + USER_ROI_CROSS_RADIUS * zoomScreenPixelWidth,
            y + USER_ROI_CROSS_RADIUS * zoomScreenPixelHeight);

    return r.contains( zoomPos.x(), zoomPos.y() );
}


bool
ViewerNode::onOverlayPenDown(double /*time*/,
                            const RenderScale & /*renderScale*/,
                            ViewIdx /*view*/,
                            const QPointF & /*viewportPos*/,
                            const QPointF & pos,
                            double /*pressure*/,
                            double /*timestamp*/,
                            PenType pen)
{
    OverlaySupport* viewport = getCurrentViewportForOverlays();
    Point pixelScale;
    viewport->getPixelScale(pixelScale.x, pixelScale.y);

    bool overlaysCaught = false;
    if (!overlaysCaught &&
        pen == ePenTypeLMB &&
        _imp->buildUserRoIOnNextPress) {
        _imp->draggedUserRoI.x1 = pos.x();
        _imp->draggedUserRoI.y1 = pos.y();
        _imp->draggedUserRoI.x2 = pos.x();
        _imp->draggedUserRoI.y2 = pos.y();
        _imp->buildUserRoIOnNextPress = false;
        _imp->uiState = eViewerNodeInteractMouseStateBuildingUserRoI;
        overlaysCaught = true;
    }

    bool userRoIEnabled = _imp->toggleUserRoIButtonKnob.lock()->getValue();
    RectD userRoI;
    if (userRoIEnabled) {
        userRoI = getUserRoI();
    }
    // Catch wipe
    bool wipeEnabled = (ViewerCompositingOperatorEnum)_imp->blendingModeChoiceKnob.lock()->getValue() != eViewerCompositingOperatorNone;
    double wipeAmount = getWipeAmount();
    double wipeAngle = getWipeAngle();
    QPointF wipeCenter = getWipeCenter();

    if ( !overlaysCaught &&
        wipeEnabled &&
        pen == ePenTypeLMB &&
        _imp->isNearbyWipeCenter(wipeCenter, pos, pixelScale.x, pixelScale.y) ) {
        _imp->uiState = eViewerNodeInteractMouseStateDraggingWipeCenter;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        wipeEnabled &&
        pen == ePenTypeLMB &&
        _imp->isNearbyWipeMixHandle(wipeCenter, wipeAngle, wipeAmount, pos, pixelScale.x, pixelScale.y) ) {
        _imp->uiState = eViewerNodeInteractMouseStateDraggingWipeMixHandle;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        wipeEnabled &&
        pen == ePenTypeLMB &&
        _imp->isNearbyWipeRotateBar(wipeCenter, wipeAngle, pos, pixelScale.x, pixelScale.y) ) {
        _imp->uiState = eViewerNodeInteractMouseStateRotatingWipeHandle;
        overlaysCaught = true;
    }


    // Catch User RoI

    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoIBottomEdge(userRoI, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the bottom edge of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiBottomEdge;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoILeftEdge(userRoI, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the left edge of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiLeftEdge;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoIRightEdge(userRoI, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the right edge of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiRightEdge;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoITopEdge(userRoI, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the top edge of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiTopEdge;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoI( (userRoI.x1 + userRoI.x2) / 2., (userRoI.y1 + userRoI.y2) / 2.,
                        pos, pixelScale.x, pixelScale.y ) ) {
            // start dragging the midpoint of the user ROI
            _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiCross;
            _imp->draggedUserRoI = userRoI;
            overlaysCaught = true;
        }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoI(userRoI.x1, userRoI.y2, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the topleft corner of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiTopLeft;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoI(userRoI.x2, userRoI.y2, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the topright corner of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiTopRight;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoI(userRoI.x1, userRoI.y1, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the bottomleft corner of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiBottomLeft;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }
    if ( !overlaysCaught &&
        pen == ePenTypeLMB &&
        userRoIEnabled &&
        _imp->isNearByUserRoI(userRoI.x2, userRoI.y1, pos, pixelScale.x, pixelScale.y) ) {
        // start dragging the bottomright corner of the user ROI
        _imp->uiState = eViewerNodeInteractMouseStateDraggingRoiBottomRight;
        _imp->draggedUserRoI = userRoI;
        overlaysCaught = true;
    }

    if ( !overlaysCaught &&
        pen == ePenTypeRMB ) {
        _imp->showRightClickMenu();
        overlaysCaught = true;
    }

    _imp->lastMousePos = pos;

    return overlaysCaught;

} // onOverlayPenDown

bool
ViewerNode::onOverlayPenMotion(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/,
                                const QPointF & /*viewportPos*/, const QPointF & pos, double /*pressure*/, double /*timestamp*/)
{
    OverlaySupport* viewport = getCurrentViewportForOverlays();
    Point pixelScale;
    viewport->getPixelScale(pixelScale.x, pixelScale.y);

    bool userRoIEnabled = _imp->toggleUserRoIButtonKnob.lock()->getValue();
    RectD userRoI;
    if (userRoIEnabled) {
        if (_imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomEdge ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopEdge ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiLeftEdge ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiRightEdge ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiCross ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomLeft ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomRight ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopLeft ||
            _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopRight) {
            userRoI = _imp->draggedUserRoI;
        } else {
            userRoI = getUserRoI();
        }
    }
    bool wipeEnabled = (ViewerCompositingOperatorEnum)_imp->blendingModeChoiceKnob.lock()->getValue() != eViewerCompositingOperatorNone;
    double wipeAmount = getWipeAmount();
    double wipeAngle = getWipeAngle();
    QPointF wipeCenter = getWipeCenter();

    bool wasHovering = _imp->hoverState != eHoverStateNothing;
    bool cursorSet = false;
    bool overlayCaught = false;
    _imp->hoverState = eHoverStateNothing;
    if ( wipeEnabled && _imp->isNearbyWipeCenter(wipeCenter, pos, pixelScale.x, pixelScale.y) ) {
        setCurrentCursor(eCursorSizeAll);
        cursorSet = true;
    } else if ( wipeEnabled && _imp->isNearbyWipeMixHandle(wipeCenter, wipeAngle, wipeAmount, pos, pixelScale.x, pixelScale.y) ) {
        _imp->hoverState = eHoverStateWipeMix;
        overlayCaught = true;
    } else if ( wipeEnabled && _imp->isNearbyWipeRotateBar(wipeCenter, wipeAngle, pos, pixelScale.x, pixelScale.y) ) {
        _imp->hoverState = eHoverStateWipeRotateHandle;
        overlayCaught = true;
    } else if (userRoIEnabled) {
        if ( _imp->isNearByUserRoIBottomEdge(userRoI, pos, pixelScale.x, pixelScale.y)
            || _imp->isNearByUserRoITopEdge(userRoI, pos, pixelScale.x, pixelScale.y)
            || ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomEdge)
            || ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopEdge) ) {
            setCurrentCursor(eCursorSizeVer);
            cursorSet = true;
        } else if ( _imp->isNearByUserRoILeftEdge(userRoI, pos, pixelScale.x, pixelScale.y)
                   || _imp->isNearByUserRoIRightEdge(userRoI, pos, pixelScale.x, pixelScale.y)
                   || ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiLeftEdge)
                   || ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiRightEdge) ) {
            setCurrentCursor(eCursorSizeHor);
            cursorSet = true;
        } else if ( _imp->isNearByUserRoI( (userRoI.x1 + userRoI.x2) / 2, (userRoI.y1 + userRoI.y2) / 2, pos, pixelScale.x, pixelScale.y )
                   || ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiCross) ) {
            setCurrentCursor(eCursorSizeAll);
            cursorSet = true;
        } else if ( _imp->isNearByUserRoI(userRoI.x2, userRoI.y1, pos, pixelScale.x, pixelScale.y) ||
                   _imp->isNearByUserRoI(userRoI.x1, userRoI.y2, pos, pixelScale.x, pixelScale.y) ||
                   ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomRight) ||
                   ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopLeft) ) {
            setCurrentCursor(eCursorFDiag);
            cursorSet = true;
        } else if ( _imp->isNearByUserRoI(userRoI.x1, userRoI.y1, pos, pixelScale.x, pixelScale.y) ||
                   _imp->isNearByUserRoI(userRoI.x2, userRoI.y2, pos, pixelScale.x, pixelScale.y) ||
                   ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomLeft) ||
                   ( _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopRight) ) {
            setCurrentCursor(eCursorBDiag);
            cursorSet = true;
        }
    }

    if (!cursorSet) {
        setCurrentCursor(eCursorDefault);
    }


    if ( (_imp->hoverState == eHoverStateNothing) && wasHovering ) {
        overlayCaught = true;
    }

    double dx = pos.x() - _imp->lastMousePos.x();
    double dy = pos.y() - _imp->lastMousePos.y();

    switch (_imp->uiState) {
        case eViewerNodeInteractMouseStateDraggingRoiBottomEdge: {
            if ( (_imp->draggedUserRoI.y1 + dy) < _imp->draggedUserRoI.y2 ) {
                _imp->draggedUserRoI.y1 += dy;
                overlayCaught = true;
            }
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiLeftEdge: {
            if ( (_imp->draggedUserRoI.x1 + dx) < _imp->draggedUserRoI.x2 ) {
                _imp->draggedUserRoI.x1 += dx;
                overlayCaught = true;
            }
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiRightEdge: {
            if ( (_imp->draggedUserRoI.x2 + dx) > _imp->draggedUserRoI.x1 ) {
                _imp->draggedUserRoI.x2 += dx;
                overlayCaught = true;
            }
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiTopEdge: {
            if ( (_imp->draggedUserRoI.y2 + dy) > _imp->draggedUserRoI.y1 ) {
                _imp->draggedUserRoI.y2 += dy;
                overlayCaught = true;
            }
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiCross: {
            _imp->draggedUserRoI.translate(dx, dy);
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiTopLeft: {
            if ( (_imp->draggedUserRoI.y2 + dy) > _imp->draggedUserRoI.y1 ) {
                _imp->draggedUserRoI.y2 += dy;
            }
            if ( (_imp->draggedUserRoI.x1 + dx) < _imp->draggedUserRoI.x2 ) {
                _imp->draggedUserRoI.x1 += dx;
            }
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiTopRight: {
            if ( (_imp->draggedUserRoI.y2 + dy) > _imp->draggedUserRoI.y1 ) {
                _imp->draggedUserRoI.y2 += dy;
            }
            if ( (_imp->draggedUserRoI.x2 + dx) > _imp->draggedUserRoI.x1 ) {
                _imp->draggedUserRoI.x2 += dx;
            }
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiBottomRight:
        case eViewerNodeInteractMouseStateBuildingUserRoI:{
            if ( (_imp->draggedUserRoI.x2 + dx) > _imp->draggedUserRoI.x1 ) {
                _imp->draggedUserRoI.x2 += dx;
            }
            if ( (_imp->draggedUserRoI.y1 + dy) < _imp->draggedUserRoI.y2 ) {
                _imp->draggedUserRoI.y1 += dy;
            }
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateDraggingRoiBottomLeft: {
            if ( (_imp->draggedUserRoI.y1 + dy) < _imp->draggedUserRoI.y2 ) {
                _imp->draggedUserRoI.y1 += dy;
            }
            if ( (_imp->draggedUserRoI.x1 + dx) < _imp->draggedUserRoI.x2 ) {
                _imp->draggedUserRoI.x1 += dx;
            }
            overlayCaught = true;

            break;
        }
        case eViewerNodeInteractMouseStateDraggingWipeCenter: {
            KnobDoublePtr centerKnob = _imp->wipeCenter.lock();
            centerKnob->setValue(centerKnob->getValue() + dx);
            centerKnob->setValue(centerKnob->getValue(1) + dy, ViewSpec::current(), 1);
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateDraggingWipeMixHandle: {
            KnobDoublePtr centerKnob = _imp->wipeCenter.lock();
            Point center;
            center.x = centerKnob->getValue();
            center.y = centerKnob->getValue(1);
            double angle = std::atan2( pos.y() - center.y, pos.x() - center.x );
            double prevAngle = std::atan2( _imp->lastMousePos.y() - center.y,
                                          _imp->lastMousePos.x() - center.x );
            KnobDoublePtr mixKnob = _imp->wipeAmount.lock();
            double mixAmount = mixKnob->getValue();
            mixAmount -= (angle - prevAngle);
            mixAmount = std::max( 0., std::min(mixAmount, 1.) );
            mixKnob->setValue(mixAmount);
            overlayCaught = true;
            break;
        }
        case eViewerNodeInteractMouseStateRotatingWipeHandle: {
            KnobDoublePtr centerKnob = _imp->wipeCenter.lock();
            Point center;
            center.x = centerKnob->getValue();
            center.y = centerKnob->getValue(1);
            double angle = std::atan2( pos.y() - center.y, pos.x() - center.x );

            KnobDoublePtr angleKnob = _imp->wipeAngle.lock();
            double closestPI2 = M_PI_2 * std::floor(angle / M_PI_2 + 0.5);
            if (std::fabs(angle - closestPI2) < 0.1) {
                // snap to closest multiple of PI / 2.
                angle = closestPI2;
            }

            angleKnob->setValue(angle);
            
            overlayCaught = true;
            break;
        }
        default:
            break;
    }
    _imp->lastMousePos = pos;
    return overlayCaught;

} // onOverlayPenMotion

bool
ViewerNode::onOverlayPenUp(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/, const QPointF & /*viewportPos*/, const QPointF & /*pos*/, double /*pressure*/, double /*timestamp*/)
{

    bool caught = false;
    if (_imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomEdge ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopEdge ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiLeftEdge ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiRightEdge ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiCross ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomLeft ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiBottomRight ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopLeft ||
        _imp->uiState == eViewerNodeInteractMouseStateDraggingRoiTopRight ||
        _imp->uiState == eViewerNodeInteractMouseStateBuildingUserRoI) {
        setUserRoI(_imp->draggedUserRoI);
        caught = true;
    }

    _imp->uiState = eViewerNodeInteractMouseStateIdle;


    return caught;
} // onOverlayPenUp

bool
ViewerNode::onOverlayPenDoubleClicked(double /*time*/,
                                       const RenderScale & /*renderScale*/,
                                       ViewIdx /*view*/,
                                       const QPointF & /*viewportPos*/,
                                       const QPointF & /*pos*/)
{
    return false;
} // onOverlayPenDoubleClicked

bool
ViewerNode::onOverlayKeyDown(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/, Key /*key*/, KeyboardModifiers /*modifiers*/)
{
    return false;
} // onOverlayKeyDown

bool
ViewerNode::onOverlayKeyUp(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/, Key /*key*/, KeyboardModifiers /*modifiers*/)
{
    return false;
} // onOverlayKeyUp

bool
ViewerNode::onOverlayKeyRepeat(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/, Key /*key*/, KeyboardModifiers /*modifiers*/)
{
    return false;
} // onOverlayKeyRepeat

bool
ViewerNode::onOverlayFocusGained(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/)
{
    return false;
} // onOverlayFocusGained

bool
ViewerNode::onOverlayFocusLost(double /*time*/, const RenderScale & /*renderScale*/, ViewIdx /*view*/)
{
    return false;
} // onOverlayFocusLost

OpenGLViewerI*
ViewerNode::getUiContext() const
{
    return _imp->uiContext;
}

void
ViewerNode::setUiContext(OpenGLViewerI* viewer)
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    _imp->uiContext = viewer;
}

void
ViewerNode::invalidateUiContext()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );
    _imp->uiContext = NULL;
}


NodePtr
ViewerNode::getCurrentAInput() const
{
    std::string curLabel = _imp->aInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe();
    if (curLabel == "-") {
        return NodePtr();
    }
    for (std::size_t i = 0; i < _imp->viewerInputs.size(); ++i) {
        if (_imp->viewerInputs[i].label == curLabel) {
            return _imp->viewerInputs[i].node.lock();
        }
    }
    return NodePtr();
}

NodePtr
ViewerNode::getCurrentBInput() const
{
    std::string curLabel = _imp->bInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe();
    if (curLabel == "-") {
        return NodePtr();
    }
    for (std::size_t i = 0; i < _imp->viewerInputs.size(); ++i) {
        if (_imp->viewerInputs[i].label == curLabel) {
            return _imp->viewerInputs[i].node.lock();
        }
    }
    return NodePtr();

}

void
ViewerNode::refreshInputFromChoiceMenu(int internalInputIdx)
{
    assert(internalInputIdx == 0 || internalInputIdx == 1);

    std::vector<NodePtr> groupInputNodes;
    getInputs(&groupInputNodes, false);

    KnobChoicePtr knob = internalInputIdx == 0 ? _imp->aInputNodeChoiceKnob.lock() : _imp->bInputNodeChoiceKnob.lock();
    std::string curLabel = internalInputIdx == 0 ? _imp->aInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe() : _imp->bInputNodeChoiceKnob.lock()->getActiveEntryText_mt_safe();

    NodePtr nodeToConnect = getInternalViewerNode()->getInputRecursive(internalInputIdx);
    if (curLabel == "-") {
        if (nodeToConnect->getEffectInstance().get() == this) {
            nodeToConnect->disconnectInput(internalInputIdx);
        } else {
            int prefInput = nodeToConnect->getPreferredInput();
            if (prefInput != -1) {
                nodeToConnect->disconnectInput(prefInput);
            }
        }

    } else {
        int groupInputIndex = -1;

        for (std::size_t i = 0; i < _imp->viewerInputs.size(); ++i) {
            if (_imp->viewerInputs[i].label == curLabel) {
                groupInputIndex = i;
                break;
            }
        }
        if (groupInputIndex < (int)groupInputNodes.size() && groupInputIndex >= 0) {


            if (nodeToConnect == _imp->internalViewerProcessNode.lock()) {
                nodeToConnect->disconnectInput(internalInputIdx);
                nodeToConnect->connectInput(groupInputNodes[groupInputIndex], internalInputIdx);
            } else {
                int prefInput = nodeToConnect->getPreferredInputForConnection();
                if (prefInput != -1) {
                    nodeToConnect->disconnectInput(prefInput);
                    nodeToConnect->connectInput(groupInputNodes[groupInputIndex], prefInput);
                }
            }
        }
    }
}

ViewerCompositingOperatorEnum
ViewerNode::getCurrentOperator() const
{
    return (ViewerCompositingOperatorEnum)_imp->blendingModeChoiceKnob.lock()->getValue();
}

void
ViewerNode::setRefreshButtonDown(bool down)
{
    _imp->refreshButtonKnob.lock()->setValue(down);
}

bool
ViewerNode::isViewersSynchroEnabled() const
{
    return _imp->syncViewersButtonKnob.lock()->getValue();
}

void
ViewerNode::setViewersSynchroEnabled(bool enabled)
{
    _imp->syncViewersButtonKnob.lock()->setValue(enabled);
}

void
ViewerNode::setPickerEnabled(bool enabled)
{
    _imp->enableInfoBarButtonKnob.lock()->setValue(enabled);
}

ViewIdx
ViewerNode::getCurrentView() const
{
    return ViewIdx(_imp->activeViewKnob.lock()->getValue());
}

void
ViewerNode::setCurrentView(ViewIdx view)
{
    _imp->activeViewKnob.lock()->setValue(view.value());
}

bool
ViewerNode::isClipToFormatEnabled() const
{
    return _imp->clipToFormatButtonKnob.lock()->getValue();
}

double
ViewerNode::getWipeAmount() const
{
    return _imp->wipeAmount.lock()->getValue();
}

double
ViewerNode::getWipeAngle() const
{
    return _imp->wipeAngle.lock()->getValue();
}

QPointF
ViewerNode::getWipeCenter() const
{
    KnobDoublePtr wipeCenter = _imp->wipeCenter.lock();
    QPointF r;
    r.rx() = wipeCenter->getValue();
    r.ry() = wipeCenter->getValue(1);
    return r;
}

bool
ViewerNode::isCheckerboardEnabled() const
{
    return _imp->enableCheckerboardButtonKnob.lock()->getValue();
}

bool
ViewerNode::isUserRoIEnabled() const
{
    return _imp->toggleUserRoIButtonKnob.lock()->getValue();
}

bool
ViewerNode::isOverlayEnabled() const
{
    return _imp->rightClickShowHideOverlays.lock()->getValue();
}

bool
ViewerNode::isFullFrameProcessingEnabled() const
{
    return _imp->fullFrameButtonKnob.lock()->getValue();
}

double
ViewerNode::getGain() const
{
    return _imp->gainSliderKnob.lock()->getValue();
}

double
ViewerNode::getGamma() const
{
    return _imp->gammaSliderKnob.lock()->getValue();
}

void
ViewerNode::resetWipe()
{
    beginChanges();
    _imp->wipeCenter.lock()->resetToDefaultValue(0);
    _imp->wipeCenter.lock()->resetToDefaultValue(1);
    _imp->wipeAngle.lock()->resetToDefaultValue(0);
    _imp->wipeAmount.lock()->resetToDefaultValue(0);
}

KnobChoicePtr
ViewerNode::getLayerKnob() const
{
    return _imp->layersKnob.lock();
}

KnobChoicePtr
ViewerNode::getAlphaChannelKnob() const
{
    return _imp->alphaChannelKnob.lock();
}

KnobIntPtr
ViewerNode::getPlaybackInPointKnob() const
{
    return _imp->inPointKnob.lock();
}

KnobIntPtr
ViewerNode::getPlaybackOutPointKnob() const
{
    return _imp->outPointKnob.lock();
}

KnobIntPtr
ViewerNode::getCurrentFrameKnob() const
{
    return _imp->curFrameKnob.lock();
}

KnobButtonPtr
ViewerNode::getTurboModeButtonKnob() const
{
    return _imp->enableTurboModeButtonKnob.lock();
}

bool
ViewerNode::isViewerPaused(int index) const
{
    return _imp->pauseButtonKnob[index].lock()->getValue();
}

RectD
ViewerNode::getUserRoI() const
{
    RectD ret;
    KnobDoublePtr btmLeft = _imp->userRoIBtmLeftKnob.lock();
    KnobDoublePtr size = _imp->userRoISizeKnob.lock();
    ret.x1 = btmLeft->getValue(0);
    ret.y1 = btmLeft->getValue(1);
    ret.x2 = ret.x1 + size->getValue(0);
    ret.y2 = ret.y1 + size->getValue(1);
    return ret;
}

void
ViewerNode::setUserRoI(const RectD& rect)
{
    KnobDoublePtr btmLeft = _imp->userRoIBtmLeftKnob.lock();
    KnobDoublePtr size = _imp->userRoISizeKnob.lock();
    btmLeft->setValues(rect.x1, rect.y1, ViewSpec::current(), eValueChangedReasonUserEdited);
    size->setValues(rect.x2 - rect.x1, rect.y2 - rect.y1, ViewSpec::current(), eValueChangedReasonUserEdited);
}

void
ViewerNode::reportStats(int time, ViewIdx view, double wallTime, const RenderStatsMap& stats)
{
    Q_EMIT renderStatsAvailable(time, view, wallTime, stats);
}

void
ViewerNode::executeDisconnectTextureRequestOnMainThread(int index,bool clearRoD)
{
    assert( QThread::currentThread() == qApp->thread() );
    OpenGLViewerI* uiContext = getUiContext();
    if (uiContext) {
        uiContext->disconnectInputTexture(index, clearRoD);
    }
}


unsigned int
ViewerNode::getProxyModeKnobMipMapLevel() const
{
    if (!_imp->toggleProxyModeButtonKnob.lock()->getValue()) {
        return 0;
    }
    int index =  _imp->proxyChoiceKnob.lock()->getValue();
    return index + 1;
}

void
ViewerNode::redrawViewer()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );
    OpenGLViewerI* uiContext = getUiContext();
    if (!uiContext) {
        return;
    }
    uiContext->redraw();
}

void
ViewerNodePrivate::getAllViewerNodes(bool inGroupOnly, std::list<ViewerNodePtr>& viewerNodes) const
{
    NodeCollectionPtr collection;
    if (inGroupOnly) {
        collection = _publicInterface->getNode()->getGroup();
    } else {
        collection = _publicInterface->getApp()->getProject();
    }
    NodesList nodes;
    if (inGroupOnly) {
        nodes = collection->getNodes();
    } else {
        collection->getNodes_recursive(nodes, false);
    }
    for (NodesList::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if (!(*it)->isActivated()) {
            continue;
        }
        ViewerNodePtr isViewer = (*it)->isEffectViewerNode();
        if (isViewer) {
            viewerNodes.push_back(isViewer);
        }
    }
}

void
ViewerNodePrivate::abortAllViewersRendering()
{
    std::list<ViewerNodePtr> viewers;
    getAllViewerNodes(false, viewers);

    playForwardButtonKnob.lock()->setValueFromPlugin(false, ViewSpec::current(), 0);
    playBackwardButtonKnob.lock()->setValueFromPlugin(false, ViewSpec::current(), 0);


    if ( _publicInterface->getApp()->isGuiFrozen() && appPTR->getCurrentSettings()->isAutoTurboEnabled() ) {
        _publicInterface->getApp()->setGuiFrozen(false);
    }

    // Abort all viewers because they are all synchronised.

    for (std::list<ViewerNodePtr>::const_iterator it = viewers.begin(); it != viewers.end(); ++it) {
        ViewerInstancePtr viewer = (*it)->getInternalViewerNode();
        if (viewer) {
            viewer->getRenderEngine()->abortRenderingNoRestart();
        }
    }
}

void
ViewerNodePrivate::startPlayback(RenderDirectionEnum direction)
{
    abortAllViewersRendering();

    if ( _publicInterface->getApp()->checkAllReadersModificationDate(true) ) {
        return;
    }
    _publicInterface->getApp()->setLastViewerUsingTimeline( getInternalViewerNode() );
    std::vector<ViewIdx> viewsToRender;
    viewsToRender.push_back(_publicInterface->getCurrentView());
    ViewerInstancePtr instance = _publicInterface->getInternalViewerNode();
    if (instance) {
        instance->getRenderEngine()->renderFromCurrentFrame(_publicInterface->getApp()->isRenderStatsActionChecked(), viewsToRender, direction);
    }

}

void
ViewerNode::refreshFps()
{
    bool fpsEnabled = _imp->enableFpsKnob.lock()->getValue();
    double fps;
    if (fpsEnabled) {
        fps = _imp->fpsKnob.lock()->getValue();
    } else {
        NodePtr input0 = getCurrentAInput();
        NodePtr input1 = getCurrentBInput();

        if (input0) {
            fps = input0->getEffectInstance()->getFrameRate();
        } else {
            if (input1) {
                fps = input1->getEffectInstance()->getFrameRate();
            } else {
                fps = getApp()->getProjectFrameRate();
            }
        }
        _imp->fpsKnob.lock()->setValue(fps);
    }
    ViewerInstancePtr viewerNode = getInternalViewerNode();
    if (!viewerNode) {
        return;
    }
    viewerNode->getRenderEngine()->setDesiredFPS(fps);

}

void
ViewerNodePrivate::timelineGoTo(double time)
{
    ViewerInstancePtr viewer = _publicInterface->getInternalViewerNode();
    viewer->getTimeline()->seekFrame(time, true, viewer, eTimelineChangeReasonOtherSeek);
}

void
ViewerNode::onEngineStarted(bool forward)
{

    std::list<ViewerNodePtr> viewers;
    _imp->getAllViewerNodes(false, viewers);
    for (std::list<ViewerNodePtr>::const_iterator it = viewers.begin(); it != viewers.end(); ++it) {
        (*it)->_imp->playForwardButtonKnob.lock()->setValueFromPlugin(forward, ViewSpec::current(), 0);
        (*it)->_imp->playBackwardButtonKnob.lock()->setValueFromPlugin(!forward, ViewSpec::current(), 0);
    }

    if (!getApp()->isGuiFrozen() && appPTR->getCurrentSettings()->isAutoTurboEnabled() ) {
        getApp()->setGuiFrozen(true);
    }
}


void
ViewerNode::onEngineStopped()
{
 

    // Don't set the playback buttons up now, do it a bit later, maybe the user will restart playback  just aftewards
    _imp->mustSetUpPlaybackButtonsTimer.start(200);

    std::list<ViewerNodePtr> viewers;
    _imp->getAllViewerNodes(false, viewers);

    _imp->curFrameKnob.lock()->setValueFromPlugin( getInternalViewerNode()->getTimeline()->currentFrame(), ViewSpec::current(), 0 );

    if (!getApp()->isGuiFrozen() && appPTR->getCurrentSettings()->isAutoTurboEnabled() ) {
        getApp()->setGuiFrozen(false);
    } else {
        getApp()->refreshAllTimeEvaluationParams(true);
    }
}


void
ViewerNode::onSetDownPlaybackButtonsTimeout()
{
    ViewerInstancePtr instance = getInternalViewerNode();
    if ( instance && instance->getRenderEngine() && !instance->getRenderEngine()->isDoingSequentialRender() ) {
        std::list<ViewerNodePtr> viewers;
        _imp->getAllViewerNodes(false, viewers);
        for (std::list<ViewerNodePtr>::const_iterator it = viewers.begin(); it != viewers.end(); ++it) {
            (*it)->_imp->playForwardButtonKnob.lock()->setValueFromPlugin(false, ViewSpec::current(), 0);
            (*it)->_imp->playBackwardButtonKnob.lock()->setValueFromPlugin(false, ViewSpec::current(), 0);
        }
    }
}

NATRON_NAMESPACE_EXIT;
NATRON_NAMESPACE_USING;
#include "moc_ViewerNode.cpp"
