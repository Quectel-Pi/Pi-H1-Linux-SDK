do_install:append:qcom(){
  sed -i \
    -e 's/#listener/listener 1883/' \
    -e 's/#allow_anonymous false/allow_anonymous true/' \
    ${D}${sysconfdir}/mosquitto/mosquitto.conf
}