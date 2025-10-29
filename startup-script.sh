#!/usr/bin/env bash
set -e

# ====================================================
# ARCH GAMING QUICKSTART INSTALLER WITH AUR FALLBACK
# ====================================================

echo "==============================================="
echo "       Arch Gaming Quickstart Installer         "
echo "==============================================="

# ------------------------
# Pre-Flight Checks
# ------------------------
echo "==> Running pre-flight checks..."

# 1. Root privileges
if [ "$EUID" -ne 0 ]; then
    echo "❌ This script must be run as root or with sudo."
    exit 1
fi
echo "✅ Root privileges OK."

# 2. Internet connectivity
if ! ping -c 1 archlinux.org &> /dev/null; then
    echo "❌ No internet connection detected. Please connect to the internet and retry."
    exit 1
fi
echo "✅ Internet connection OK."

# 3. Minimum disk space (30GB)
MIN_SPACE=$((30 * 1024 * 1024)) # 30GB in KB
AVAILABLE_SPACE=$(df / | tail -1 | awk '{print $4}')
if [ "$AVAILABLE_SPACE" -lt "$MIN_SPACE" ]; then
    echo "❌ Not enough free disk space. At least 30GB required."
    exit 1
fi
echo "✅ Disk space check passed (Available: $(df -h / | tail -1 | awk '{print $4}'))."

# 4. yay presence (optional, installer handles auto-install anyway)
if command -v yay &> /dev/null; then
    echo "✅ yay AUR helper detected."
else
    echo "ℹ️ yay not found, will install automatically if needed."
fi

echo "==> Pre-flight checks complete. Continuing with installation..."

# ------------------------
# Helper: install_pkg
# ------------------------
install_pkg() {
    PKG="$1"
    echo "==> Installing $PKG..."
    if ! sudo pacman -S --needed --noconfirm "$PKG"; then
        echo "Package $PKG not found in official repos. Trying AUR..."
        if ! command -v yay &> /dev/null; then
            echo "yay not found. Installing yay first..."
            git clone https://aur.archlinux.org/yay.git /tmp/yay
            cd /tmp/yay
            makepkg -si --noconfirm
            cd ~
            rm -rf /tmp/yay
        fi
        yay -S --needed --noconfirm "$PKG"
    fi
}

# ------------------------
# 0. Prerequisites
# ------------------------
for pkg in git base-devel wget curl unzip sudo bash zsh; do
    install_pkg "$pkg"
done

# ------------------------
# 1. CPU Detection & Microcode
# ------------------------
CPU_VENDOR=$(lscpu | grep "Vendor ID" | awk '{print $3}')
echo "Detected CPU vendor: $CPU_VENDOR"

if [[ "$CPU_VENDOR" == "AuthenticAMD" || "$CPU_VENDOR" == "AMD" ]]; then
    install_pkg amd-ucode
    echo "Applying AMD CPU optimizations..."
    echo "options amd_pstate=active" | sudo tee /etc/modprobe.d/amd_pstate.conf
    cat <<EOF | sudo tee /etc/sysctl.d/99-amd-performance.conf
kernel.nmi_watchdog = 0
kernel.sched_migration_cost_ns = 5000000
vm.swappiness = 10
EOF
elif [[ "$CPU_VENDOR" == "GenuineIntel" || "$CPU_VENDOR" == "Intel" ]]; then
    install_pkg intel-ucode
    echo "Applying Intel CPU optimizations..."
    echo "options intel_pstate=enable" | sudo tee /etc/modprobe.d/intel_pstate.conf
    cat <<EOF | sudo tee /etc/sysctl.d/99-intel-performance.conf
kernel.nmi_watchdog = 0
kernel.sched_autogroup_enabled = 0
vm.swappiness = 10
EOF
fi

# Enable performance governor globally
cat <<'EOF' | sudo tee /etc/udev/rules.d/99-cpu-governor.rules
SUBSYSTEM=="cpu", ACTION=="add", KERNEL=="cpu[0-9]*", ATTR{cpufreq/scaling_governor}="performance"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger

# ------------------------
# 2. GPU Detection & Drivers
# ------------------------
GPU_INFO=$(lspci | grep -E "VGA|3D")
echo "Detected GPU: $GPU_INFO"

if echo "$GPU_INFO" | grep -qi "NVIDIA"; then
    for pkg in nvidia nvidia-utils nvidia-settings lib32-nvidia-utils vulkan-icd-loader; do
        install_pkg "$pkg"
    done
elif echo "$GPU_INFO" | grep -qi "AMD"; then
    for pkg in mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon vulkan-tools; do
        install_pkg "$pkg"
    done
elif echo "$GPU_INFO" | grep -qi "Intel"; then
    for pkg in mesa lib32-mesa vulkan-intel lib32-vulkan-intel vulkan-tools; do
        install_pkg "$pkg"
    done
else
    for pkg in mesa lib32-mesa vulkan-icd-loader lib32-vulkan-icd-loader; do
        install_pkg "$pkg"
    done
fi

# ------------------------
# 3. System Services
# ------------------------
SYSTEM_SERVICES=(
    networkmanager
    bluez bluez-utils bluedevil
    pipewire pipewire-alsa pipewire-pulse wireplumber pavucontrol helvum easyeffects
    sddm timeshift htop fstrim.timer
)
for pkg in "${SYSTEM_SERVICES[@]}"; do
    install_pkg "$pkg"
done

# Enable essential services
sudo systemctl enable NetworkManager
sudo systemctl enable bluetooth
sudo systemctl enable sddm
sudo systemctl enable fstrim.timer

# Firewall
read -p "Enable UFW firewall? [Y/n]: " UFW_CHOICE
if [[ "$UFW_CHOICE" =~ ^[Yy]$ || -z "$UFW_CHOICE" ]]; then
    install_pkg ufw
    sudo systemctl enable ufw
    sudo ufw enable
fi

# Laptop detection
if grep -q "Battery" /sys/class/power_supply/*/type 2>/dev/null; then
    echo "Laptop detected — installing TLP..."
    install_pkg tlp
    sudo systemctl enable tlp
fi

# ------------------------
# 4. Gaming Stack
# ------------------------
GAMING_STACK=(
    steam lutris gamemode mangohud lib32-gamemode lib32-mangohud vkbasalt
    wine-staging wine-gecko wine-mono winetricks protonup-qt
)
for pkg in "${GAMING_STACK[@]}"; do
    install_pkg "$pkg"
done

sudo systemctl enable --now gamemoded

# Optional launchers
read -p "Install Heroic Games Launcher? [Y/n]: " HEROIC
if [[ "$HEROIC" =~ ^[Yy]$ || -z "$HEROIC" ]]; then
    install_pkg heroic-games-launcher-bin
fi

read -p "Install Itch.io Launcher? [Y/n]: " ITCH
if [[ "$ITCH" =~ ^[Yy]$ || -z "$ITCH" ]]; then
    install_pkg itch
fi

read -p "Install OBS Studio? [Y/n]: " OBS
if [[ "$OBS" =~ ^[Yy]$ || -z "$OBS" ]]; then
    install_pkg obs-studio
fi

# ------------------------
# 5. Desktop Environment
# ------------------------
for pkg in plasma plasma-wayland-session kde-applications xorg xorg-xinit xterm; do
    install_pkg "$pkg"
done

# ------------------------
# 6. Filesystem & Bootloader
# ------------------------
echo "==> Optimizing filesystem..."
ROOT_FS=$(findmnt -n -o FSTYPE /)
if ! grep -q "noatime" /etc/fstab; then
    sudo sed -i 's/rw/rw,noatime/g' /etc/fstab
fi
sudo systemctl enable fstrim.timer

# Bootloader
if [ -d /sys/firmware/efi ]; then
    BOOTMODE="UEFI"
else
    BOOTMODE="BIOS"
fi

for pkg in grub os-prober efibootmgr; do
    install_pkg "$pkg"
done

if [[ "$BOOTMODE" == "UEFI" ]]; then
    sudo grub-install --target=x86_64-efi --efi-directory=/boot --bootloader-id=Arch
else
    ROOT_DEV=$(lsblk -no pkname $(df / | tail -1 | awk '{print $1}'))
    sudo grub-install --target=i386-pc /dev/"$ROOT_DEV"
fi
sudo grub-mkconfig -o /boot/grub/grub.cfg

# ------------------------
# 7. Daemon Theme Pack + Kvantum
# ------------------------
echo "==> Installing Daemon theme pack..."
git clone https://github.com/MathisP75/daemon-kde-mk2.git ~/daemon-theme
cd ~/daemon-theme

mkdir -p ~/.local/share/color-schemes
cp "Color Scheme/Daemon2.colors" ~/.local/share/color-schemes/

mkdir -p ~/.local/share/plasma/desktoptheme
cp -r "Plasma Style/Daemon-2.0" ~/.local/share/plasma/desktoptheme/

mkdir -p ~/.local/share/icons
cp -r "Icon Theme/Daemon-Icons" ~/.local/share/icons/

mkdir -p ~/.local/share/aurorae/themes
cp -r "Window Decorations/daemon-2.0" ~/.local/share/aurorae/themes/

mkdir -p ~/.local/share/konsole
cp Konsole/Daemon-2.0.colorscheme ~/.local/share/konsole/

mkdir -p ~/.config/Kvantum/daemon-2.0
cp -r Kvantum/daemon-2.0 ~/.config/Kvantum/

kwriteconfig5 --file kdeglobals --group General --key colorScheme "Daemon2"
kwriteconfig5 --file kdeglobals --group Icons --key IconTheme "Daemon-Icons"
kwriteconfig5 --file kwinrc --group org.kde.kdecoration2 --key theme "daemon-2.0"
kwriteconfig5 --file plasmarc --group Theme --key name "Daemon-2.0"
kwriteconfig5 --file Kvantum/kvantum.kvconfig --group Theme --key theme "daemon-2.0"

cd ~
rm -rf ~/daemon-theme

# ------------------------
# 8. UX Polish
# ------------------------
echo "==> Applying fonts, aliases, wallpaper, and Plasma tweaks..."

cat <<'EOF' >> ~/.bashrc
alias ll='ls -lah --color=auto'
alias update='sudo pacman -Syu'
alias fixpacman='sudo rm /var/lib/pacman/db.lck'
alias mirrors='sudo reflector --latest 10 --sort rate --save /etc/pacman.d/mirrorlist'
alias cleanup='sudo pacman -Rns $(pacman -Qtdq)'
alias gamemode='systemctl start gamemoded'
alias protonup='protonup-qt'
EOF

WALLPAPER_URL="https://raw.githubusercontent.com/adi1090x/wallpapers/master/minimalistic/019.jpg"
mkdir -p ~/Pictures
curl -Lo ~/Pictures/wallpaper.jpg "$WALLPAPER_URL"
kwriteconfig5 --file plasmarc --group Theme --key defaultWallpaper "$HOME/Pictures/wallpaper.jpg"

kwriteconfig5 --file kwinrc --group Compositing --key OpenGLIsUnsafe false
kwriteconfig5 --file kwinrc --group Compositing --key Backend OpenGL
kwriteconfig5 --file kwinrc --group Compositing --key GLCore true
kwriteconfig5 --file kwinrc --group Compositing --key AnimationSpeed 0

mkdir -p ~/.config/fontconfig
cat <<'EOF' > ~/.config/fontconfig/fonts.conf
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <alias>
    <family>sans-serif</family>
    <prefer><family>Rajdhani</family></prefer>
  </alias>
  <alias>
    <family>serif</family>
    <prefer><family>Rajdhani</family></prefer>
  </alias>
  <alias>
    <family>monospace</family>
    <prefer><family>DejaVu Sans Mono</family></prefer>
  </alias>
</fontconfig>
EOF

# ------------------------
# 9. Final Summary Splash
# ------------------------
clear
echo "=================================================="
echo "           ✅ ARCH GAMING QUICKSTART DONE          "
echo "=================================================="
echo "Desktop: KDE Plasma"
echo "Font: Rajdhani"
echo "Gaming Stack: Steam + Lutris + Proton-GE + Vulkan"
echo "Audio: PipeWire + WirePlumber + EasyEffects + Helvum"
echo "Performance: GameMode + MangoHUD + vkBasalt"
echo
echo "Optional Tools Installed:"
[[ "$UFW_CHOICE" =~ ^[Yy]$ || -z "$UFW_CHOICE" ]] && echo " • Firewall (UFW)"
[[ "$HEROIC" =~ ^[Yy]$ ]] && echo " • Heroic Games Launcher"
[[ "$ITCH" =~ ^[Yy]$ ]] && echo " • Itch.io"
[[ "$OBS" =~ ^[Yy]$ ]] && echo " • OBS Studio"
echo
echo "Daemon theme applied system-wide!"
echo "Reboot now to start your optimized Arch experience!"
echo "=================================================="
