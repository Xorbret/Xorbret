Hello! I am new to the Linux environment and have done a ton of research and testing through Nobara, Fedora, Ubuntu, and Arch. I wanted to expedite future Arch installs on gaming hardware and provide an easy stepping off point for my friends.

This script will install what I deem necessary for a good out-the-box gaming experience while staying in Arch. This assumes you have already mounted your drives and partitions and installed:

base
linux
linux-lts
linux-firmware
networkmanager
dhcpcd
sudo
git
curl
wget
base-devel base
linux
linux-lts
linux-firmware
networkmanager
dhcpcd
base-devel
sudo
wget /or/ curl

Once these are installed just run the following command (I added logic for both wget and curl)

bash <(command -v curl >/dev/null 2>&1 && curl -sSL https://raw.githubusercontent.com/Xorbret/Xorbret/6c9474bbc9e9d5cdaeedcaeefb3576c401f7692c/startup-script.sh || wget -qO- https://raw.githubusercontent.com/Xorbret/Xorbret/6c9474bbc9e9d5cdaeedcaeefb3576c401f7692c/startup-script.sh )
