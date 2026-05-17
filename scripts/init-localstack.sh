#!/bin/bash
# Runs inside LocalStack on startup — creates the S3 bucket
awslocal s3 mb s3://raftdrive-objects --region us-east-1
echo "[LocalStack] Bucket raftdrive-objects created."
