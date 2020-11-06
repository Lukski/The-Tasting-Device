using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class TasteController : SerialController{
    /**
     * class that represents a command, with all the information still needed, once the command has been sent.
     */
    private class TastingDeviceCommand{
        public int commandNr;
        public float timestamp;
        public CallbackFunction onSuccess;
        public CallbackFunction onFailure;
        public CallbackFunction onTimeout;

        public TastingDeviceCommand(int commandNr, CallbackFunction onSuccess, CallbackFunction onFailure,
            CallbackFunction onTimeout){
            this.commandNr = commandNr;
            this.timestamp = Time.time;
            this.onSuccess = onSuccess;
            this.onFailure = onFailure;
            this.onTimeout = onTimeout;
        }
    }
    
    public delegate void CallbackFunction(string message);
    
    private int commandNr = 0;

    private Boolean isConnected = false;

    [Tooltip("The timeout when waiting for responses to a command, in seconds")]
    public float timeout = 10;
    
    private List<TastingDeviceCommand> commandList = new List<TastingDeviceCommand>();
    
    [Tooltip("The maximum number of commands, the API will listen for at the same time")]
    public int maxCommandQueueLength = 100;

    public static (int, int, int) presetSour = (50, 50, 50);

    public static (int, int, int) presetBitter = (100, 50, 0);
    
    void Start(){
        this.SetTearDownFunction(Shutdown);
    }

    void RequireIntRange(int value, int min, int max){
        if (value < min || value > max){
            throw new ArgumentException($"Invalid value {value}. Must be in range [{min},{max}]");
        }
    }

    public void Activate(int dacValue, int dutyCycle, int frequency,
        CallbackFunction onSuccess=null, CallbackFunction onFailure=null, CallbackFunction onTimeout=null){
        RequireIntRange(dacValue,0,100);
        RequireIntRange(dutyCycle,0,100);
        RequireIntRange(frequency,0,100);
        this.SendCommand(dacValue, dutyCycle, frequency, onSuccess, onFailure, onTimeout);
    }

    public void ActivatePreset((int, int, int) preset,
        CallbackFunction onSuccess=null, CallbackFunction onFailure=null, CallbackFunction onTimeout=null){
        Activate(preset.Item1, preset.Item2, preset.Item3, onSuccess, onFailure, onTimeout);
    }
    
    public void Deactivate(CallbackFunction onSuccess=null, CallbackFunction onFailure=null, CallbackFunction onTimeout=null){
        this.SendCommand(0,0,100, onSuccess, onFailure, onTimeout);
    }

    public void Shutdown(){
        this.Deactivate();
    }

    private void SendCommand(int dacValue, int dutyCycle, int frequency,
        CallbackFunction onSuccess=null, CallbackFunction onFailure=null, CallbackFunction onTimeout=null){
        if (onSuccess == null)
            onSuccess = this.DefaultOnSuccess;
        if (onFailure == null)
            onFailure = this.DefaultOnFailure;
        if (onTimeout == null)
            onTimeout = this.DefaultOnTimeout;

        int currentCommand = this.commandNr;
        // check if commandlist is full and if so remove the oldest command
        if (this.commandList.Count >= maxCommandQueueLength){
            this.commandList[0].onTimeout("command response timed out, too many requests in queue");
            this.commandList.RemoveAt(0);
        }
        this.commandList.Add(new TastingDeviceCommand(currentCommand, onSuccess, onFailure, onTimeout));
        this.SendSerialMessage($"{currentCommand} {dacValue} {dutyCycle} {frequency}");
        this.commandNr++;
        if (this.commandNr >= maxCommandQueueLength){
            this.commandNr = 0;
        }
    }
    
    void Update()
    {
        // Read the next message from the queue
        string message = (string)serialThread.ReadMessage();
        if (message == null)
            return;

        // Check if the message is plain data or a connect/disconnect event.
        if (ReferenceEquals(message, SERIAL_DEVICE_CONNECTED))
            this.OnConnectionEvent(true);
        else if (ReferenceEquals(message, SERIAL_DEVICE_DISCONNECTED))
            this.OnConnectionEvent(false);
        else
            this.OnMessageArrived(message);

        // Check if any commands timed out
        while (commandList.Count > 0 && commandList[0].timestamp + timeout <= Time.time){
            commandList[0].onTimeout("command response timed out");
            commandList.RemoveAt(0);
        }
    }

    void OnConnectionEvent(Boolean connected){
        Debug.Log("Tasting device " + (connected ? "connected" : "disconnected"));
        this.isConnected = connected;
    }

    void OnMessageArrived(string message){
        if (message != null){
            int index = message.IndexOf(' ');
            if (index != -1){
                string commandNrStr = message.Substring(0, index);
                if (int.TryParse(commandNrStr, out int receivedCommandNr)){
                    for (int i=0; i<commandList.Count; i++){
                        TastingDeviceCommand cmd = commandList[i];
                        if (cmd.commandNr == receivedCommandNr){
                            message = message.Substring(index+1);
                            commandList.RemoveAt(i);
                            if (message.Equals("success\r")){
                                cmd.onSuccess(message);
                            }
                            else{
                                cmd.onFailure(message);
                            }
                            return;
                        }
                    }
                }
            }
        }
        Debug.Log("error");
    }


    void DefaultOnSuccess(string message){
    }
    
    void DefaultOnFailure(string message){
        Debug.Log(message);
    }
    
    void DefaultOnTimeout(string message){
        Debug.Log(message);
    }

}
