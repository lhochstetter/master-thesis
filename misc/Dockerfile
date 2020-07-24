FROM debian:buster-slim

# install required packages
RUN apt-get -qq update \
	&& apt-get -y dist-upgrade \
	&& apt-get install --no-install-recommends -y openssh-client build-essential gcc git wget make libncurses-dev flex bison gperf python python-pip python-setuptools python-serial python-cryptography python-future \
	&& apt-get -y  autoremove \
	&& apt-get autoclean \
	&& rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# download the cross compiler
RUN wget https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz \
	&& tar -xzf xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz -C /opt \
	&& rm xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz

# download the ESP IDF source code
RUN git clone -b v3.3.1 --recursive https://github.com/espressif/esp-idf.git \
	&& cd /esp-idf/ \
	&& git submodule update --init --recursive

RUN pip install wheel \
	&& wget https://raw.githubusercontent.com/espressif/esp-idf/v3.3.1/requirements.txt \
	&& /bin/bash -c "/usr/bin/python -m pip install -r requirements.txt" \
	&& rm requirements.txt
	#&& /bin/bash -c "/usr/bin/python -m pip install --user -r requirements.txt" \

RUN chmod -R a+rwx /esp-idf/

# set required paths
ENV IDF_PATH /esp-idf/
ENV PATH /opt/xtensa-esp32-elf/bin:$PATH
ENV PWD /app/
