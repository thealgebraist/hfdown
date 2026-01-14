#!/usr/bin/env python3
"""
Basic validation tests for Vast.ai Flux orchestration scripts
Tests basic functionality without requiring a real Vast.ai instance
"""

import json
import os
import sys
import tempfile
from pathlib import Path

# Test configuration loading
def test_config_loading():
    """Test that configuration files can be loaded"""
    print("Testing configuration loading...")
    
    config_path = "vastai_config.example.json"
    if not os.path.exists(config_path):
        print(f"  ✗ Example config not found: {config_path}")
        return False
    
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        # Check required keys
        required_keys = ["remote_workspace", "num_processes", "output_dir"]
        for key in required_keys:
            if key not in config:
                print(f"  ✗ Missing required config key: {key}")
                return False
        
        print(f"  ✓ Configuration loaded successfully")
        return True
    except Exception as e:
        print(f"  ✗ Failed to load config: {e}")
        return False


def test_prompts_file():
    """Test that example prompts file exists and is readable"""
    print("Testing prompts file...")
    
    prompts_path = "prompts.example.txt"
    if not os.path.exists(prompts_path):
        print(f"  ✗ Example prompts not found: {prompts_path}")
        return False
    
    try:
        with open(prompts_path, 'r') as f:
            prompts = [line.strip() for line in f if line.strip()]
        
        if len(prompts) == 0:
            print(f"  ✗ No prompts found in {prompts_path}")
            return False
        
        print(f"  ✓ Prompts file loaded successfully ({len(prompts)} prompts)")
        return True
    except Exception as e:
        print(f"  ✗ Failed to load prompts: {e}")
        return False


def test_script_imports():
    """Test that all scripts can be imported without errors"""
    print("Testing script imports...")
    
    scripts = [
        "vastai_flux_orchestrator",
        "flux_generator",
        "resource_monitor"
    ]
    
    all_ok = True
    for script_name in scripts:
        try:
            __import__(script_name)
            print(f"  ✓ {script_name}.py imports successfully")
        except Exception as e:
            print(f"  ✗ {script_name}.py import failed: {e}")
            all_ok = False
    
    return all_ok


def test_orchestrator_init():
    """Test that orchestrator can be initialized"""
    print("Testing orchestrator initialization...")
    
    try:
        from vastai_flux_orchestrator import VastAIOrchestrator
        
        # Test with default config
        orch = VastAIOrchestrator("ssh -p 12345 root@example.com")
        
        if orch.config is None:
            print("  ✗ Orchestrator config is None")
            return False
        
        print("  ✓ Orchestrator initialized successfully")
        return True
    except Exception as e:
        print(f"  ✗ Orchestrator initialization failed: {e}")
        return False


def test_ssh_parsing():
    """Test SSH command parsing"""
    print("Testing SSH command parsing...")
    
    try:
        from vastai_flux_orchestrator import VastAIOrchestrator
        
        # Test various SSH command formats
        test_cases = [
            "ssh -p 12345 root@example.com",
            "ssh -p12345 root@example.com",
            "ssh root@example.com",
            "ssh -i key.pem -p 12345 root@example.com"
        ]
        
        all_ok = True
        for ssh_cmd in test_cases:
            try:
                orch = VastAIOrchestrator(ssh_cmd)
                # Just test that it doesn't crash
                print(f"  ✓ Parsed: {ssh_cmd}")
            except Exception as e:
                print(f"  ✗ Failed to parse '{ssh_cmd}': {e}")
                all_ok = False
        
        return all_ok
    except Exception as e:
        print(f"  ✗ SSH parsing test failed: {e}")
        return False


def test_requirements_file():
    """Test that requirements file exists"""
    print("Testing requirements file...")
    
    req_path = "requirements-vastai.txt"
    if not os.path.exists(req_path):
        print(f"  ✗ Requirements file not found: {req_path}")
        return False
    
    print(f"  ✓ Requirements file exists")
    return True


def test_documentation():
    """Test that documentation files exist"""
    print("Testing documentation...")
    
    docs = [
        "VASTAI_FLUX_README.md",
        "README.md"
    ]
    
    all_ok = True
    for doc in docs:
        if not os.path.exists(doc):
            print(f"  ✗ Documentation not found: {doc}")
            all_ok = False
        else:
            print(f"  ✓ {doc} exists")
    
    return all_ok


def test_setup_script():
    """Test that setup script exists and is executable"""
    print("Testing setup script...")
    
    script_path = "setup_vastai.sh"
    if not os.path.exists(script_path):
        print(f"  ✗ Setup script not found: {script_path}")
        return False
    
    if not os.access(script_path, os.X_OK):
        print(f"  ✗ Setup script is not executable")
        return False
    
    print(f"  ✓ Setup script exists and is executable")
    return True


def main():
    """Run all tests"""
    print("=" * 60)
    print("Vast.ai Flux Orchestration - Validation Tests")
    print("=" * 60)
    print()
    
    tests = [
        ("Configuration Loading", test_config_loading),
        ("Prompts File", test_prompts_file),
        ("Script Imports", test_script_imports),
        ("Orchestrator Init", test_orchestrator_init),
        ("SSH Parsing", test_ssh_parsing),
        ("Requirements File", test_requirements_file),
        ("Documentation", test_documentation),
        ("Setup Script", test_setup_script),
    ]
    
    results = []
    for test_name, test_func in tests:
        print()
        try:
            result = test_func()
            results.append((test_name, result))
        except Exception as e:
            print(f"  ✗ Test crashed: {e}")
            results.append((test_name, False))
    
    print()
    print("=" * 60)
    print("Test Results")
    print("=" * 60)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for test_name, result in results:
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"  {status}: {test_name}")
    
    print()
    print(f"Total: {passed}/{total} tests passed")
    print("=" * 60)
    
    # Return success if all tests passed
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
