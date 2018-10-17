from threading import Thread
import subprocess
import sys
import os
import signal
from main import COMMAND,OUT_FILE
    

def on_server_loaded(server_context):
    #''' If present, this function is called when the server first starts. '''
    pass

def on_server_unloaded(server_context):
    #''' If present, this function is called when the server shuts down. '''
    ## UNCOMMEND TO DELETE THE LAST LIST OF EVENTS
    #try:
    #    os.remove(os.path.join(os.path.dirname(__file__),OUT_FILE))
    #except:
    #    pass
    pass
    
def on_session_created(session_context):
    #''' If present, this function is called when a session is created. '''
    #print("session_created", file=sys.stderr)
    pass

def on_session_destroyed(session_context):
    #''' If present, this function is called when a session is closed. '''
    #print("session_destroyed", file=sys.stderr)
    # Esto tampoco anda..
    pass
