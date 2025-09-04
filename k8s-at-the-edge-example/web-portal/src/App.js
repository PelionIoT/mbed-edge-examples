import React, { useState, useEffect } from 'react';
import axios from 'axios';
import { 
  Lightbulb, 
  ToggleLeft, 
  Thermometer, 
  Droplets, 
  Plus,
  RefreshCw,
  Settings
} from 'lucide-react';

const API_BASE_URL = process.env.REACT_APP_API_URL || 'http://localhost:3000';

function App() {
  const [devices, setDevices] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [showAddDevice, setShowAddDevice] = useState(false);
  const [newDevice, setNewDevice] = useState({ name: '', deviceType: 'LightBulb' });

  useEffect(() => {
    fetchDevices();
  }, []);

  const fetchDevices = async () => {
    try {
      setLoading(true);
      const response = await axios.get(`${API_BASE_URL}/devices`);
      if (response.data.success) {
        setDevices(response.data.data);
      }
    } catch (err) {
      setError('Failed to fetch devices');
      console.error('Error fetching devices:', err);
    } finally {
      setLoading(false);
    }
  };

  const updateDeviceState = async (deviceId, newState) => {
    try {
      const response = await axios.put(`${API_BASE_URL}/devices/${deviceId}/state`, {
        state: newState
      });
      if (response.data.success) {
        setDevices(devices.map(device => 
          device.id === deviceId ? response.data.data : device
        ));
      }
    } catch (err) {
      setError('Failed to update device state');
      console.error('Error updating device:', err);
    }
  };

  const createDevice = async () => {
    try {
      const response = await axios.post(`${API_BASE_URL}/devices`, newDevice);
      if (response.data.success) {
        setDevices([response.data.data, ...devices]);
        setNewDevice({ name: '', deviceType: 'LightBulb' });
        setShowAddDevice(false);
      }
    } catch (err) {
      setError('Failed to create device');
      console.error('Error creating device:', err);
    }
  };

  const getDeviceIcon = (deviceType) => {
    switch (deviceType) {
      case 'LightBulb':
        return <Lightbulb className="w-6 h-6" />;
      case 'Switch':
        return <ToggleLeft className="w-6 h-6" />;
      case 'TemperatureSensor':
        return <Thermometer className="w-6 h-6" />;
      case 'HumiditySensor':
        return <Droplets className="w-6 h-6" />;
      default:
        return <Settings className="w-6 h-6" />;
    }
  };

  const renderDeviceControl = (device) => {
    switch (device.device_type) {
      case 'LightBulb':
      case 'Switch':
        const isOn = device.state.on;
        return (
          <button
            onClick={() => updateDeviceState(device.id, { on: !isOn })}
            className={`px-4 py-2 rounded-lg font-medium transition-colors ${
              isOn 
                ? 'bg-green-500 text-white hover:bg-green-600' 
                : 'bg-gray-300 text-gray-700 hover:bg-gray-400'
            }`}
          >
            {isOn ? 'ON' : 'OFF'}
          </button>
        );
      
      case 'TemperatureSensor':
        return (
          <div className="text-2xl font-bold text-blue-600">
            {device.state.temperature}°C
          </div>
        );
      
      case 'HumiditySensor':
        return (
          <div className="text-2xl font-bold text-cyan-600">
            {device.state.humidity}%
          </div>
        );
      
      default:
        return <div className="text-gray-500">Unknown device type</div>;
    }
  };

  if (loading) {
    return (
      <div className="min-h-screen bg-gray-50 flex items-center justify-center">
        <div className="text-center">
          <RefreshCw className="w-8 h-8 animate-spin mx-auto mb-4 text-blue-600" />
          <p className="text-gray-600">Loading devices...</p>
        </div>
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-gray-50">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8">
        {/* Header */}
        <div className="mb-8">
          <h1 className="text-3xl font-bold text-gray-900 mb-2">
            Device Management Portal
          </h1>
          <p className="text-gray-600">
            Manage your virtual IoT devices
          </p>
        </div>

        {/* Error Message */}
        {error && (
          <div className="mb-6 bg-red-50 border border-red-200 rounded-lg p-4">
            <p className="text-red-800">{error}</p>
            <button 
              onClick={() => setError(null)}
              className="text-red-600 hover:text-red-800 text-sm mt-2"
            >
              Dismiss
            </button>
          </div>
        )}

        {/* Add Device Button */}
        <div className="mb-6">
          <button
            onClick={() => setShowAddDevice(true)}
            className="inline-flex items-center px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors"
          >
            <Plus className="w-4 h-4 mr-2" />
            Add Device
          </button>
        </div>

        {/* Add Device Modal */}
        {showAddDevice && (
          <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
            <div className="bg-white rounded-lg p-6 w-full max-w-md">
              <h2 className="text-xl font-semibold mb-4">Add New Device</h2>
              <div className="space-y-4">
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-1">
                    Device Name
                  </label>
                  <input
                    type="text"
                    value={newDevice.name}
                    onChange={(e) => setNewDevice({...newDevice, name: e.target.value})}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                    placeholder="Enter device name"
                  />
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-1">
                    Device Type
                  </label>
                  <select
                    value={newDevice.deviceType}
                    onChange={(e) => setNewDevice({...newDevice, deviceType: e.target.value})}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  >
                    <option value="LightBulb">Light Bulb</option>
                    <option value="Switch">Switch</option>
                    <option value="TemperatureSensor">Temperature Sensor</option>
                    <option value="HumiditySensor">Humidity Sensor</option>
                  </select>
                </div>
                <div className="flex space-x-3 pt-4">
                  <button
                    onClick={createDevice}
                    disabled={!newDevice.name.trim()}
                    className="flex-1 px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700 disabled:bg-gray-300 disabled:cursor-not-allowed"
                  >
                    Create
                  </button>
                  <button
                    onClick={() => setShowAddDevice(false)}
                    className="flex-1 px-4 py-2 bg-gray-300 text-gray-700 rounded-md hover:bg-gray-400"
                  >
                    Cancel
                  </button>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Devices Grid */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-6">
          {devices.map((device) => (
            <div
              key={device.id}
              className="bg-white rounded-lg shadow-md p-6 hover:shadow-lg transition-shadow"
            >
              <div className="flex items-center justify-between mb-4">
                <div className="flex items-center space-x-3">
                  <div className="text-gray-600">
                    {getDeviceIcon(device.device_type)}
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900">{device.name}</h3>
                    <p className="text-sm text-gray-500 capitalize">
                      {device.device_type.replace(/([A-Z])/g, ' $1').trim()}
                    </p>
                  </div>
                </div>
              </div>
              
              <div className="mb-4">
                {renderDeviceControl(device)}
              </div>
              
              <div className="text-xs text-gray-400">
                Last updated: {new Date(device.updated_at).toLocaleString()}
              </div>
            </div>
          ))}
        </div>

        {/* Empty State */}
        {devices.length === 0 && !loading && (
          <div className="text-center py-12">
            <Settings className="w-12 h-12 text-gray-400 mx-auto mb-4" />
            <h3 className="text-lg font-medium text-gray-900 mb-2">No devices found</h3>
            <p className="text-gray-600 mb-4">
              Get started by adding your first device
            </p>
            <button
              onClick={() => setShowAddDevice(true)}
              className="inline-flex items-center px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700"
            >
              <Plus className="w-4 h-4 mr-2" />
              Add Device
            </button>
          </div>
        )}
      </div>
    </div>
  );
}

export default App; 