FROM gcc

COPY . /usr/src/swift
WORKDIR /usr/src/swift

RUN make

ENTRYPOINT [ "./swift" ]
