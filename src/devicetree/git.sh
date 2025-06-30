list=" camera-devicetree  display-devicetree  graphics-devicetree  video-devicetree  wlan-devicetree"
for f in ${list}
do
	git -C ${f} status 
	git -C ${f} diff
done
