#creates window
import tkinter as tk
window = tk.Tk()

#top of window label
label = tk.Label(text="AsCaulk Industries")

#internal label
label = tk.Label(
    text="AsCaulk welcomes you",
    fg="white", #set text color
    bg="black", #set background color
)
label.pack()

#Clickable button
button = tk.Button(
    text="AsCaulk",
    width=25,
    height=5,
    bg="blue",
    fg="yellow",
)
button.pack()

#loop window
window.mainloop()
