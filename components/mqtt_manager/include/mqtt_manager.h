#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

void mqtt_app_start(void);

void mqtt_manager_publish_sensor_data(const void *data_ptr);

#endif /* MQTT_MANAGER_H */