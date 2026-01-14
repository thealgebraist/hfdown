.PHONY: all build generate server benchmark profile clean

BIN = ./build/hfdown
DATA_DIR = test_server_data_benchmark
MODEL_ID = test/random-model
PORT = 8891
SERVER_LOG = server.log

all: build benchmark

build:
	mkdir -p build && cd build && cmake .. && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

generate:
	python3 generate_random_benchmark_data.py --out-dir $(DATA_DIR) --model-name $(MODEL_ID) --num-small 200 --num-large 20

server: generate
	@echo "Starting local HF server on port $(PORT)..."
	@python3 test_server.py $(DATA_DIR) > $(SERVER_LOG) 2>&1 & echo $$! > server.pid
	@sleep 5

server-no-gen:
	@rm -f server.port
	@echo "Starting local HF server on dynamic port..."
	@python3 test_server.py $(DATA_DIR) > $(SERVER_LOG) 2>&1 & echo $$! > server.pid
	@while [ ! -f server.port ]; do sleep 0.5; done
	@echo "Server started on port `cat server.port`"

benchmark: build server
	@echo "Running benchmark..."
	@PORT=`cat server.port`; \
	python3 benchmark_ttest.py --binary $(BIN) --model-id $(MODEL_ID) --iterations 10 --mirror http://localhost:$$PORT
	@$(MAKE) stop-server

extreme-benchmark: build
	@echo "Preparing extreme benchmark data (1024 small files + 512MB, 1GB, 2GB with RANDOM bytes)..."
	rm -rf $(DATA_DIR)
	python3 generate_random_benchmark_data.py --out-dir $(DATA_DIR) --model-name $(MODEL_ID) \
		--num-small 1024 --small-size-kb 1.0 \
		--large-sizes-mb 512 1024 2048 --random-large
	@$(MAKE) server-no-gen
	@PORT=`cat server.port`; \
	echo "Running extreme benchmark on port $$PORT (Timeout 60s)..."; \
	for t in 1 8; do \
		echo "Testing with $$t threads..."; \
		rm -rf test_download_$$t; \
		timeout -s 9 60s $(BIN) download $(MODEL_ID) test_download_$$t --mirror http://localhost:$$PORT --threads $$t || echo "Download timed out or failed"; \
		find test_download_$$t -type f | wc -l | xargs echo "Files downloaded:"; \
		du -sh test_download_$$t 2>/dev/null || echo "Directory empty"; \
	done
	@$(MAKE) stop-server

profile: build server
	@echo "Running profile with 'sample' (macOS specific)..."
	-rm -rf test_download_profile
	-$(BIN) download $(MODEL_ID) test_download_profile --mirror http://localhost:$(PORT) > /dev/null & \
	PID=$$!; \
	sample $$PID 5 -file profile.txt; \
	wait $$PID
	@$(MAKE) stop-server
	@echo "Profile saved to profile.txt"

stop-server:
	@if [ -f server.pid ]; then \
		kill `cat server.pid` && rm server.pid; \
		echo "Server stopped."; \
	fi

clean: stop-server
	rm -rf $(DATA_DIR) test_download_* $(SERVER_LOG) profile.txt
	rm -rf build
