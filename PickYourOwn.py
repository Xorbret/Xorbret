import tkinter as tk
from tkinter import messagebox as tkMessageBox
window1 = tk.Tk()

label = tk.Label(
    text="Welcome! \n You find yourself in a prison cell, afraid and alone. \n You are wearing ragged clothes but do not know how you got them. \n Your memory is gone and your head is throbbing. \n What do you do?",
)
label.pack()

def deathcallback():
    tkMessageBox.showinfo("Uh oh...", "You Died!")

def window2():
    window1.destroy()
    window2 = tk.Tk()
    def window4():
        window2.destroy()
        window4 = tk.Tk()
        def window5():
            window4.destroy()
            window5 = tk.Tk()
            label = tk.Label(
                text="The guard reels in pain as the acid burns at his face."
            )
            label.pack()
            But = tk.Button(
                text="Grab the keys"
            )
            But.pack()
        label = tk.Label(
            text="'Hmph. Weakling' \n The guard walks away for a few minutes. \n When he returns he pushes a wooden tankard filled with water. \n 'Hurry up, you're cutting into my drinking time'"
        )
        label.pack()
        Bu = tk.Button(
            text="Drink the water",
            command = deathcallback,
        )
        Bu2 = tk.Button(
            text="Splash the water in his face.",
            command = window5,
        )
        Bu.pack()
        Bu2.pack()
    def window3():
        window2.destroy()
        window3 = tk.Tk()
        def window6():
            window3.destroy()
            window6 = tk.Tk()
            label = tk.Label(
                text="'Hmmm... Come with me, we need to talk to the warden.' \n The guard unlocks the door and shoves you out. \n He leads you down a long, dark hallway to a large door. \n He opens the door and pushes you through. \n Before you is an older woman with a scarred face. \n 'Why do you bring me a prisoner?' she asks \n 'This one is claiming amnesia. I think some Arcane Intervention is in order.'"
            )
            label.pack()
            NewButt = tk.Button(
                text="Stay silent."
            )
            NewButt.pack()
        label = tk.Label(
            text="'That's what they all say' he grunts. \n The guard looks to both the left and the right. \n 'Memory loss is a dangerous game to play, prisoner. What's the last thing you rememember?'"
        )
        label.pack()
        Butto = tk.Button(
            text="'I don't remember a thing! You have to help me!'",
            command = window6,
        )
        Butto2 = tk.Button(
            text="'I remember banging your mom last night!'",
            command = deathcallback,
        )
        Butto.pack()
        Butto2.pack()
    label = tk.Label(
        text="A guard comes up to the bars and screams out, \n 'Shut your mouth you dirty pest!' \n What do you do?",
    )
    label.pack()
    Butt = tk.Button(
        text="'Please, sir! I have no idea why I'm here!'",
        command = window3,
    )
    Butt2 = tk.Button(
        text="Punch him in his stupid face.",
        command = deathcallback,
    )
    Butt3 = tk.Button(
        text="'I think I might be getting sick. Can I have a glass of water?'",
        command = window4,
    )
    Butt.pack()
    Butt2.pack()
    Butt3.pack()

Button = tk.Button(
    text="Bash head against wall",
    bg="#FF00FF",
    fg="#000080",
    command = deathcallback,
)
Button2 = tk.Button(
    text="Call out to someone",
    bg="#FF00FF",
    fg="#000080",
    command = window2
)
Button3 = tk.Button(
    text="Shake the bars",
    bg="#FF00FF",
    fg="#000080",
    command = window2
)
Button.pack()
Button2.pack()
Button3.pack()
