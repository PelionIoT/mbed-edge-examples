JOBS:=$(shell nproc)

default: build-all-examples-debug

all: build-all-examples build-all-examples-debug build-doc

clean:
	rm -r -f build-doc
	rm -r -f build
	rm -r -f debug-debug

generate-cli-parsers:
	cd pt-example && ./gen_docopt.sh
	cd mqttpt-example && ./gen_docopt.sh
	cd c-api-stress-tester && ./gen_docopt.sh
	cd blept-example && ./gen_docopt.sh

build:
	mkdir -p build

build-debug:
	mkdir -p build-debug

build-sanitize:
	mkdir -p build-sanitize

initialize-cmake-build: build
	cd build && cmake .. && cd ..

build-pt-example: initialize-cmake-build generate-cli-parsers
	cd build && cmake .. && make pt-example -j ${JOBS} && cd ..

build-mqttpt-example: initialize-cmake-build generate-cli-parsers
	cd build && cmake .. && make mqttpt-example -j ${JOBS} && cd ..

build-blept-example: initialize-cmake-build generate-cli-parsers
	cd build && cmake .. && make blept-example -j ${JOBS} && cd ..

build-c-api-stress-tester: initialize-cmake-build generate-cli-parsers
	cd build && cmake .. && make c-api-stress-tester -j ${JOBS} && cd ..

build-all-examples: build-pt-example build-mqttpt-example build-blept-example

build-doc:
	mkdir -p build-doc && cd build-doc && cmake .. && make edge-examples-doc
	echo "\033[0;33mDocumentation is at ./build-doc/doxygen/index.html\033[0m"

initialize-cmake-sanitize-build: build-sanitize
	cd build-sanitize && cmake .. -DENABLE_THREAD_SANITIZE=1 -DTRACE_LEVEL=DEBUG -DCMAKE_BUILD_TYPE=Debug && cd ..

build-pt-example-sanitize: initialize-cmake-sanitize-build generate-cli-parsers
	cd build-sanitize && VERBOSE=1 make pt-example -j ${JOBS} && cd ..

build-mqttpt-example-sanitize: initialize-cmake-sanitize-build generate-cli-parsers
	cd build-sanitize && make mqttpt-example -j ${JOBS} && cd ..

build-blept-example-sanitize: initialize-cmake-sanitize-build generate-cli-parsers
	cd build-sanitize && cmake .. && make blept-example -j ${JOBS} && cd ..

build-c-api-stress-tester-sanitize: initialize-cmake-sanitize-build generate-cli-parsers
	cd build-sanitize && make c-api-stress-tester -j ${JOBS} && cd ..

initialize-cmake-debug-build: build-debug
	cd build-debug && cmake .. -DTRACE_LEVEL=INFO -DCMAKE_BUILD_TYPE=Debug && cd ..

build-pt-example-debug: initialize-cmake-debug-build generate-cli-parsers
	cd build-debug && make pt-example -j ${JOBS} && cd ..

build-mqttpt-example-debug: initialize-cmake-debug-build generate-cli-parsers
	cd build-debug && make mqttpt-example -j ${JOBS} && cd ..

build-blept-example-debug: initialize-cmake-debug-build generate-cli-parsers
	cd build-debug && make blept-example -j ${JOBS} && cd ..
build-c-api-stress-tester-debug: initialize-cmake-debug-build generate-cli-parsers
	cd build-debug && make c-api-stress-tester -j ${JOBS} && cd ..

build-all-examples: build-pt-example build-mqttpt-example build-blept-example build-c-api-stress-tester

build-all-examples-debug: build-pt-example-debug build-mqttpt-example-debug build-blept-example-debug build-c-api-stress-tester-debug

build-all-examples-sanitize: build-pt-example-sanitize build-mqttpt-example-sanitize build-blept-example-sanitize build-c-api-stress-tester-sanitize
