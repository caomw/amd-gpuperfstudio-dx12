//==============================================================================
// Copyright (c) 2013-2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  Layer manager base class. Contains common functionality required
///         by all derived layer managers
//==============================================================================

#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H


#include "ILayer.h"
#include <vector>
#include "CommandProcessor.h"

class CaptureLayer;
class FrameStatsLogger;

/// Layer description
struct LAYERDESC
{
    const char* tagName;        ///< an string identifier for this layer manager instance
    const char* displayName;    ///< the displayable name corresponding to the tag name
    const char* pID;            ///< process ID of application this server is running in
    UIDisplayMode eDisplayMode; ///< flag indicating if the item is to be displayed in the client
    ILayer* Layer;              ///< pointer to the layer
    CommandProcessor* CmdProcessor; ///< the command processor for this layer description
};

typedef std::vector< ILayer* > LayerStack;

/// Manages the base functionality of maintaining a stack of available and enabled layers.
/// A LayerManager should be considered as the main Layer to a GPS2 Plugin.
class LayerManager: public ILayer, public CommandProcessor
{
protected:

    /// stack of available layers (which are not enabled)
    LayerStack m_AvailableLayers;

    /// stack of currently enabled layers
    LayerStack m_EnabledLayers;

    /// flag to indicate if any layers (and which ones) should be disabled (primarily for debugging purposes).
    unsigned int m_DebugFlag;

    CommandResponse m_showStack;            ///< Command to display the current layer stack
    CommandResponse m_popLayer;             ///< Command to disable the top layer on the layer stack
    TextCommandResponse m_pushLayer;        ///< Command to add a layer to the top of the layer stack
    StepFrameCommandResponse m_stepFrame;   ///< Command to step to next frame

    LAYERDESC* m_LayerList;    ///< list of available layers
    size_t m_LayerListSize;    ///< Number of available layers

    // Instant capture state
    enum eInstantCaptureState
    {
        INSTANT_CAPTURE_ALLOW,          ///< Instant capture is allowed
        INSTANT_CAPTURE_CAPTURED,       ///< Instant capture has captured a frame and is playing it back
    };

public:

    /// Constructor
    LayerManager();

    /// Called when a known device / context is created
    /// \param type the type of object that was created
    /// \param pPtr pointer to the object
    bool OnCreate(CREATION_TYPE type, void* pPtr) = 0;

    /// Called when a known device / context is destroyed
    /// \param type the type of object that is being destroyed
    /// \param pPtr pointer to the object
    bool OnDestroy(CREATION_TYPE type, void* pPtr) = 0;

    /// Gets called at the beginning of the frame
    virtual void BeginFrame();

    /// Gets called at the end of the frame
    virtual void EndFrame();

    /// A handler invoked when Autocapture mode has been triggered.
    virtual void AutocaptureTriggered();

    /// Enables the specified layer
    bool PushLayer(ILayer& rLayer, CommandResponse* pRequest);

    /// Disables the top enabled layer
    bool PopEnabledLayer();

    /// accessor for m_DebugFlag
    unsigned int GetDebugFlag() { return m_DebugFlag; }

    /// accessor for frame count
    unsigned int GetFrameCount() { return m_frameCount; }

    //-----------------------------------------------------------------------------
    /// Inform the layer manager that a capture has started
    //-----------------------------------------------------------------------------
    void StartCapture();

    //-----------------------------------------------------------------------------
    /// Inform the layer manager that a capture has ended
    //-----------------------------------------------------------------------------
    void ReleaseCapture();

    //--------------------------------------------------------------------------
    /// Check if the current frame is the capture frame.
    /// \returns True if the current frame count matches the target capture frame.
    //--------------------------------------------------------------------------
    bool IsAutocaptureFrame()
    {
        return (m_captureFrame >= 0) && (m_frameCount == m_captureFrame);
    }

protected:
    /// How many GPUs are currently being used
    IntCommandResponse m_NumGPUs;

    /// Has a server autocapture been done without the client attached
    BoolCommandResponse m_AutoCapture;

    /// A Stats logger that can be used within every Server plugin.
    static FrameStatsLogger* mFrameStatsLogger;

private:
    //-----------------------------------------------------------------------------
    /// Send a single client command to the server.
    /// \param layerString String corresponding to the layer this command is sent to
    /// \param commandString the command to send
    /// \return true if the command is sent, false otherwise
    //-----------------------------------------------------------------------------
    bool SendServerCommand(const char* layerString, const char* commandString);

    //-----------------------------------------------------------------------------
    /// Setup an instant capture. This performs the same process as connecting the
    /// client and server before a capture. Since the client isn't used during
    /// an instant capture, this setup still needs to be done.
    //-----------------------------------------------------------------------------
    void SetupInstantCapture();

    //-----------------------------------------------------------------------------
    /// Do the frame capture. This performs the same process as pressing the pause
    /// button from the client.
    /// \return true if successful, false if error
    //-----------------------------------------------------------------------------
    bool DoInstantCapture();

    //-----------------------------------------------------------------------------
    // No additional settings
    //-----------------------------------------------------------------------------
    virtual string GetDerivedSettings() { return ""; }

    //--------------------------------------------------------------------------
    /// Keep track of how many frames have been presented.
    //--------------------------------------------------------------------------
    int    m_frameCount;                        // the total number of frames rendered by the app so far
    int    m_captureFrame;                      // index of frame to capture

    eInstantCaptureState m_instantCaptureState; // is instance capture allowed. Can only capture once for each server/app launch

    /// Has a server autocapture been done with flatten command lists enable? This flag has been set up in the capture layer, but
    /// is reset once processed so if that value is used, the value sent back to the client will always be false
    BoolCommandResponse m_FlattenCommandLists;
};

#endif //LAYERMANAGER_H
