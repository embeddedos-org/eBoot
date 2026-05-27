import React from 'react';
import { StyleSheet, Text, View, TouchableOpacity, ScrollView } from 'react-native';

export default function App() {
  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>eBoot Companion App</Text>
        <Text style={styles.subtitle}>Production Ready v3.0.1</Text>
      </View>
      <View style={styles.card}>
        <Text style={styles.cardTitle}>📡 GPS Location Tracking</Text>
        <Text style={styles.cardText}>
          Location-aware updates are active. Automatically selecting nearest CDN and matching regulatory domain.
        </Text>
      </View>
      <View style={styles.card}>
        <Text style={styles.cardTitle}>🔌 Bluetooth BLE Console</Text>
        <Text style={styles.cardText}>
          Connect directly to eBoot OTA interfaces to monitor device boot sequence and upload signed firmware.
        </Text>
        <TouchableOpacity style={styles.button}>
          <Text style={styles.buttonText}>Scan BLE Devices</Text>
        </TouchableOpacity>
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0d1117',
    padding: 20,
  },
  header: {
    marginTop: 40,
    marginBottom: 20,
  },
  title: {
    color: '#58a6ff',
    fontSize: 24,
    fontWeight: 'bold',
  },
  subtitle: {
    color: '#8b949e',
    fontSize: 14,
  },
  card: {
    backgroundColor: '#161b22',
    borderRadius: 8,
    padding: 15,
    marginBottom: 15,
    borderWidth: 1,
    borderColor: '#30363d',
  },
  cardTitle: {
    color: '#c9d1d9',
    fontSize: 16,
    fontWeight: 'bold',
    marginBottom: 8,
  },
  cardText: {
    color: '#8b949e',
    fontSize: 14,
    lineHeight: 20,
  },
  button: {
    backgroundColor: '#238636',
    borderRadius: 6,
    padding: 10,
    marginTop: 12,
    alignItems: 'center',
  },
  buttonText: {
    color: 'white',
    fontWeight: 'bold',
  },
});
