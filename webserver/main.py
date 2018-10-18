import numpy as np
import csv
import os
import subprocess
from threading import Thread
import signal
from bokeh.io import curdoc
from bokeh.layouts import row, widgetbox, column
from bokeh.models import ColumnDataSource
from bokeh.models.widgets import Slider, TextInput, Tabs, Panel, Button, CheckboxButtonGroup, RangeSlider, RadioButtonGroup
from bokeh.plotting import figure
from itertools import zip_longest

## Ajustando constantes importantes
OUT_FILE = "events.dat"
EVENTS_DIR = "events"
COMMAND = "./simulate"
MASK_FILE = "mask.mask"

## Medir
button_begin_measure = RadioButtonGroup(labels=["Parar", "Medir"], active=0)
slider_gain = Slider(start=1, end=1023, value=1, step=1, title="Ganancia")
slider_evsize = Slider(start=1, end=100, value=2, step=1, title="Tamaño Mínimo")
slider_trigger = RangeSlider(start=0, end=1023, value=(150,200), step=1, title="Umbral de detección")

def execute(cmd):
    popen = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line
        if (button_begin_measure.active == 0):
            popen.kill()
    popen.stdout.close()
    return_code = popen.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)

def cmdtofile(cmd, filename):
    try: 
        fileout = open(os.path.join(os.path.dirname(__file__),filename),"w")
        for path in execute(cmd):          # time, size, nsatPixels, charge, coord_x, coord_y
            print(path, end="")
            fileout.write(path)
            fileout.flush()
        fileout.close()
    except KeyboardInterrupt:
        fileout.close()
    except:
        print("Proceso Terminado")
        fileout.close()

def begin_measure():
    if button_begin_measure.active == 1:
        try:
            cmd = COMMAND
            cmd += " -g "+str(int(slider_gain.value))
            cmd += " -eva "+str(int(slider_evsize.value))
            cmd += " -evth "+str(int(slider_trigger.value[0]))
            cmd += " -evtr "+str(int(slider_trigger.value[1]))
            cmd += " -mask "+MASK_FILE
            print("Ejecutando: "+cmd)
            thread = Thread(target=cmdtofile, args=(cmd,OUT_FILE))
            thread.start()
        except:
            pass

button_begin_measure.on_change('active', lambda attr, old, new: begin_measure())
tab3 = widgetbox(button_begin_measure,slider_gain,slider_evsize,slider_trigger)

print("Esto es una prueba")

# Reading data
if os.path.getsize(os.path.join(os.path.dirname(__file__),OUT_FILE)) != 0:
    with open(os.path.join(os.path.dirname(__file__),OUT_FILE),"r") as csv_file:
        data =  np.genfromtxt(csv_file, delimiter='\t')
else:
    data = []
    
if data != []:
    n_events = len(data[:,0])
else:
    n_events = 0

# Set up plot
p_size_histogram = figure(plot_height=400, plot_width=400, title="Histograma de tamaños", tools="crosshair,save")
if data != []:
    size_histo = np.bincount(data[:,2].astype(int))
else:
    size_histo = [0]
p_size_histogram.step(range(0,len(size_histo)), size_histo)
tab1 = Panel(child=p_size_histogram, title="Tamaño")

## CHARGE_HISTOGRAM
button_update = Button(label="Actualizar eventos")

def update_charge_histogram(attrname, old, new):
    y = np.add.reduceat(charge_histo, range(0,len(charge_histo),new))
    source_charge_histogram.data = dict(x=range(0,len(y)*new,new), y=y)

p_charge_histogram = figure(plot_height=400, plot_width=400, title="Histograma de cargas",
        tools="reset,xwheel_zoom,xpan,save", active_scroll="xwheel_zoom", output_backend="svg")
if data != []:
    charge_histo = np.bincount(data[:,3].astype(int))
else:
    charge_histo = [0]
source_charge_histogram = ColumnDataSource(data=dict(x=range(0,len(charge_histo)), y=charge_histo))
p_charge_histogram.step('x','y',source=source_charge_histogram, mode="center")

log_y_axis_charge_histogram = RadioButtonGroup(labels=["Lineal", "Log"], active=0)
slider_charge_histo_bin = Slider(title="Ancho del bin", value=1, start=1, end=64, step=1)
slider_charge_histo_bin.on_change('value',update_charge_histogram)
tab2 = Panel(child=column(widgetbox(button_update,slider_charge_histo_bin),
                    p_charge_histogram), title="Carga")

def update_event(attrname, old, new):
    with open(os.path.join(os.path.dirname(__file__),"events",new+".pgm"),"r") as csv_file:
        lines = csv_file.readlines()
    meta_data = np.fromstring(lines[4][1:], dtype=float, sep='\t')
            #Time	Size	Charge	Origin_x	Origin_y	Center_x	Center_y	Threshold
    #image_event = np.genfromtxt(lines[5:], delimiter='\t').astype(int)
    largo = len(np.fromstring(lines[5][:], dtype=float, sep='\t'))
    alto = len(np.fromstring(lines[5:][0], dtype=float, sep='\t'))
    p_event.x_range.start = meta_data[3]; p_event.x_range.end = meta_data[3]+largo
    p_event.y_range.start = meta_data[4]; p_event.y_range.end = meta_data[4]+alto
    source_image_event.data = dict(x=[meta_data[3]],y=[meta_data[4]],dw=[largo],dh=[alto],
        image=[np.genfromtxt(lines[5:], delimiter='\t').astype(int)])


#image_event = np.genfromtxt(lines[5:], delimiter='\t').astype(int)
source_image_event = ColumnDataSource(data=dict(x=[],y=[],dw=[],dh=[],image=[]))
p_event = figure(plot_height=400, plot_width=400, title="Evento", tooltips=[("x", "$x"), ("y", "$y"), ("value", "@image")], x_range=(0,1), y_range=(0,1))
update_event(0,0,"0")
# must give a vector of image data for image parameter
p_event.image(image='image', source=source_image_event, x='x', y='y', dw='dw', dh='dh', palette="Greys256")

text_event = TextInput(value="0", title="Evento:")
text_event.on_change('value', update_event)

## UPDATE
def update_data(attrname, old, new):
    return

# Set up layouts and add to document
def update_events():
    global charge_histo, n_events
    if os.path.getsize(os.path.join(os.path.dirname(__file__),OUT_FILE)) != 0:
        with open(os.path.join(os.path.dirname(__file__),OUT_FILE),"r") as csv_file:
            data =  np.genfromtxt(csv_file, delimiter='\t')
    else:
        data = []
    if data == []:
        return
    toAdd = np.bincount(data[n_events:,3].astype(int))
    charge_histo = [sum(n) for n in zip_longest(charge_histo, toAdd, fillvalue=0)]
    n_events = len(data[:,0])
    global slider_charge_histo_bin
    update_charge_histogram(0,0,slider_charge_histo_bin.value)

button_update.on_click(update_events)

curdoc().add_root(row(tab3, Tabs(tabs=[ tab2, tab1 ], width=500),column(widgetbox(text_event),p_event)))
curdoc().title = "Radmonpi++"